// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitmap.h>
#include <linux/cpumask.h>
#include <linux/memblock.h>
#include <linux/err.h>
#include <linux/lcm.h>
#include <linux/list.h>
#include <linux/log2.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/pfn.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/kmemleak.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/memcontrol.h>

#include <asm/cacheflush.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>
#include <asm/io.h>

#include "percpu-internal.h"

#define PCPU_SLOT_BASE_SHIFT		5

#define PCPU_SLOT_FAIL_THRESHOLD	3

#define PCPU_EMPTY_POP_PAGES_LOW	2
#define PCPU_EMPTY_POP_PAGES_HIGH	4

#define __addr_to_pcpu_ptr(addr)	(void __percpu *)(addr)
#define __pcpu_ptr_to_addr(ptr)		(void __force *)(ptr)

static int pcpu_unit_pages __ro_after_init;
static int pcpu_unit_size __ro_after_init;
static int pcpu_nr_units __ro_after_init;
static int pcpu_atom_size __ro_after_init;
int pcpu_nr_slots __ro_after_init;
static int pcpu_free_slot __ro_after_init;
int pcpu_sidelined_slot __ro_after_init;
int pcpu_to_depopulate_slot __ro_after_init;
static size_t pcpu_chunk_struct_size __ro_after_init;

static unsigned int pcpu_low_unit_cpu __ro_after_init;
static unsigned int pcpu_high_unit_cpu __ro_after_init;

void *pcpu_base_addr __ro_after_init;

static const int *pcpu_unit_map __ro_after_init;		
const unsigned long *pcpu_unit_offsets __ro_after_init;	

static int pcpu_nr_groups __ro_after_init;
static const unsigned long *pcpu_group_offsets __ro_after_init;
static const size_t *pcpu_group_sizes __ro_after_init;

struct pcpu_chunk *pcpu_first_chunk __ro_after_init;

struct pcpu_chunk *pcpu_reserved_chunk __ro_after_init;

DEFINE_SPINLOCK(pcpu_lock);	
static DEFINE_MUTEX(pcpu_alloc_mutex);	

struct list_head *pcpu_chunk_lists __ro_after_init; 

static LIST_HEAD(pcpu_map_extend_chunks);

int pcpu_nr_empty_pop_pages;

static unsigned long pcpu_nr_populated;

static void pcpu_balance_workfn(struct work_struct *work);
static DECLARE_WORK(pcpu_balance_work, pcpu_balance_workfn);
static bool pcpu_async_enabled __read_mostly;
static bool pcpu_atomic_alloc_failed;

static void pcpu_schedule_balance_work(void)
{
	if (pcpu_async_enabled)
		schedule_work(&pcpu_balance_work);
}

static bool pcpu_addr_in_chunk(struct pcpu_chunk *chunk, void *addr)
{
	void *start_addr, *end_addr;

	if (!chunk)
		return false;

	start_addr = chunk->base_addr + chunk->start_offset;
	end_addr = chunk->base_addr + chunk->nr_pages * PAGE_SIZE -
		   chunk->end_offset;

	return addr >= start_addr && addr < end_addr;
}

static int __pcpu_size_to_slot(int size)
{
	int highbit = fls(size);	
	return max(highbit - PCPU_SLOT_BASE_SHIFT + 2, 1);
}

static int pcpu_size_to_slot(int size)
{
	if (size == pcpu_unit_size)
		return pcpu_free_slot;
	return __pcpu_size_to_slot(size);
}

static int pcpu_chunk_slot(const struct pcpu_chunk *chunk)
{
	const struct pcpu_block_md *chunk_md = &chunk->chunk_md;

	if (chunk->free_bytes < PCPU_MIN_ALLOC_SIZE ||
	    chunk_md->contig_hint == 0)
		return 0;

	return pcpu_size_to_slot(chunk_md->contig_hint * PCPU_MIN_ALLOC_SIZE);
}

static void pcpu_set_page_chunk(struct page *page, struct pcpu_chunk *pcpu)
{
	page->index = (unsigned long)pcpu;
}

static struct pcpu_chunk *pcpu_get_page_chunk(struct page *page)
{
	return (struct pcpu_chunk *)page->index;
}

static int __maybe_unused pcpu_page_idx(unsigned int cpu, int page_idx)
{
	return pcpu_unit_map[cpu] * pcpu_unit_pages + page_idx;
}

static unsigned long pcpu_unit_page_offset(unsigned int cpu, int page_idx)
{
	return pcpu_unit_offsets[cpu] + (page_idx << PAGE_SHIFT);
}

static unsigned long pcpu_chunk_addr(struct pcpu_chunk *chunk,
				     unsigned int cpu, int page_idx)
{
	return (unsigned long)chunk->base_addr +
	       pcpu_unit_page_offset(cpu, page_idx);
}

static unsigned long *pcpu_index_alloc_map(struct pcpu_chunk *chunk, int index)
{
	return chunk->alloc_map +
	       (index * PCPU_BITMAP_BLOCK_BITS / BITS_PER_LONG);
}

static unsigned long pcpu_off_to_block_index(int off)
{
	return off / PCPU_BITMAP_BLOCK_BITS;
}

static unsigned long pcpu_off_to_block_off(int off)
{
	return off & (PCPU_BITMAP_BLOCK_BITS - 1);
}

static unsigned long pcpu_block_off_to_off(int index, int off)
{
	return index * PCPU_BITMAP_BLOCK_BITS + off;
}

static bool pcpu_check_block_hint(struct pcpu_block_md *block, int bits,
				  size_t align)
{
	int bit_off = ALIGN(block->contig_hint_start, align) -
		block->contig_hint_start;

	return bit_off + bits <= block->contig_hint;
}

static int pcpu_next_hint(struct pcpu_block_md *block, int alloc_bits)
{
	
	if (block->scan_hint &&
	    block->contig_hint_start > block->scan_hint_start &&
	    alloc_bits > block->scan_hint)
		return block->scan_hint_start + block->scan_hint;

	return block->first_free;
}

static void pcpu_next_md_free_region(struct pcpu_chunk *chunk, int *bit_off,
				     int *bits)
{
	int i = pcpu_off_to_block_index(*bit_off);
	int block_off = pcpu_off_to_block_off(*bit_off);
	struct pcpu_block_md *block;

	*bits = 0;
	for (block = chunk->md_blocks + i; i < pcpu_chunk_nr_blocks(chunk);
	     block++, i++) {
		
		if (*bits) {
			*bits += block->left_free;
			if (block->left_free == PCPU_BITMAP_BLOCK_BITS)
				continue;
			return;
		}

		
		*bits = block->contig_hint;
		if (*bits && block->contig_hint_start >= block_off &&
		    *bits + block->contig_hint_start < PCPU_BITMAP_BLOCK_BITS) {
			*bit_off = pcpu_block_off_to_off(i,
					block->contig_hint_start);
			return;
		}
		
		block_off = 0;

		*bits = block->right_free;
		*bit_off = (i + 1) * PCPU_BITMAP_BLOCK_BITS - block->right_free;
	}
}

static void pcpu_next_fit_region(struct pcpu_chunk *chunk, int alloc_bits,
				 int align, int *bit_off, int *bits)
{
	int i = pcpu_off_to_block_index(*bit_off);
	int block_off = pcpu_off_to_block_off(*bit_off);
	struct pcpu_block_md *block;

	*bits = 0;
	for (block = chunk->md_blocks + i; i < pcpu_chunk_nr_blocks(chunk);
	     block++, i++) {
		
		if (*bits) {
			*bits += block->left_free;
			if (*bits >= alloc_bits)
				return;
			if (block->left_free == PCPU_BITMAP_BLOCK_BITS)
				continue;
		}

		
		*bits = ALIGN(block->contig_hint_start, align) -
			block->contig_hint_start;
		
		if (block->contig_hint &&
		    block->contig_hint_start >= block_off &&
		    block->contig_hint >= *bits + alloc_bits) {
			int start = pcpu_next_hint(block, alloc_bits);

			*bits += alloc_bits + block->contig_hint_start -
				 start;
			*bit_off = pcpu_block_off_to_off(i, start);
			return;
		}
		
		block_off = 0;

		*bit_off = ALIGN(PCPU_BITMAP_BLOCK_BITS - block->right_free,
				 align);
		*bits = PCPU_BITMAP_BLOCK_BITS - *bit_off;
		*bit_off = pcpu_block_off_to_off(i, *bit_off);
		if (*bits >= alloc_bits)
			return;
	}

	
	*bit_off = pcpu_chunk_map_bits(chunk);
}

#define pcpu_for_each_md_free_region(chunk, bit_off, bits)		\
	for (pcpu_next_md_free_region((chunk), &(bit_off), &(bits));	\
	     (bit_off) < pcpu_chunk_map_bits((chunk));			\
	     (bit_off) += (bits) + 1,					\
	     pcpu_next_md_free_region((chunk), &(bit_off), &(bits)))

#define pcpu_for_each_fit_region(chunk, alloc_bits, align, bit_off, bits)     \
	for (pcpu_next_fit_region((chunk), (alloc_bits), (align), &(bit_off), \
				  &(bits));				      \
	     (bit_off) < pcpu_chunk_map_bits((chunk));			      \
	     (bit_off) += (bits),					      \
	     pcpu_next_fit_region((chunk), (alloc_bits), (align), &(bit_off), \
				  &(bits)))

static void *pcpu_mem_zalloc(size_t size, gfp_t gfp)
{
	if (WARN_ON_ONCE(!slab_is_available()))
		return NULL;

	if (size <= PAGE_SIZE)
		return kzalloc(size, gfp);
	else
		return __vmalloc(size, gfp | __GFP_ZERO);
}

static void pcpu_mem_free(void *ptr)
{
	kvfree(ptr);
}

static void __pcpu_chunk_move(struct pcpu_chunk *chunk, int slot,
			      bool move_front)
{
	if (chunk != pcpu_reserved_chunk) {
		if (move_front)
			list_move(&chunk->list, &pcpu_chunk_lists[slot]);
		else
			list_move_tail(&chunk->list, &pcpu_chunk_lists[slot]);
	}
}

static void pcpu_chunk_move(struct pcpu_chunk *chunk, int slot)
{
	__pcpu_chunk_move(chunk, slot, true);
}

static void pcpu_chunk_relocate(struct pcpu_chunk *chunk, int oslot)
{
	int nslot = pcpu_chunk_slot(chunk);

	
	if (chunk->isolated)
		return;

	if (oslot != nslot)
		__pcpu_chunk_move(chunk, nslot, oslot < nslot);
}

static void pcpu_isolate_chunk(struct pcpu_chunk *chunk)
{
	lockdep_assert_held(&pcpu_lock);

	if (!chunk->isolated) {
		chunk->isolated = true;
		pcpu_nr_empty_pop_pages -= chunk->nr_empty_pop_pages;
	}
	list_move(&chunk->list, &pcpu_chunk_lists[pcpu_to_depopulate_slot]);
}

static void pcpu_reintegrate_chunk(struct pcpu_chunk *chunk)
{
	lockdep_assert_held(&pcpu_lock);

	if (chunk->isolated) {
		chunk->isolated = false;
		pcpu_nr_empty_pop_pages += chunk->nr_empty_pop_pages;
		pcpu_chunk_relocate(chunk, -1);
	}
}

static inline void pcpu_update_empty_pages(struct pcpu_chunk *chunk, int nr)
{
	chunk->nr_empty_pop_pages += nr;
	if (chunk != pcpu_reserved_chunk && !chunk->isolated)
		pcpu_nr_empty_pop_pages += nr;
}

static inline bool pcpu_region_overlap(int a, int b, int x, int y)
{
	return (a < y) && (x < b);
}

static void pcpu_block_update(struct pcpu_block_md *block, int start, int end)
{
	int contig = end - start;

	block->first_free = min(block->first_free, start);
	if (start == 0)
		block->left_free = contig;

	if (end == block->nr_bits)
		block->right_free = contig;

	if (contig > block->contig_hint) {
		
		if (start > block->contig_hint_start) {
			if (block->contig_hint > block->scan_hint) {
				block->scan_hint_start =
					block->contig_hint_start;
				block->scan_hint = block->contig_hint;
			} else if (start < block->scan_hint_start) {
				
				block->scan_hint = 0;
			}
		} else {
			block->scan_hint = 0;
		}
		block->contig_hint_start = start;
		block->contig_hint = contig;
	} else if (contig == block->contig_hint) {
		if (block->contig_hint_start &&
		    (!start ||
		     __ffs(start) > __ffs(block->contig_hint_start))) {
			
			block->contig_hint_start = start;
			if (start < block->scan_hint_start &&
			    block->contig_hint > block->scan_hint)
				block->scan_hint = 0;
		} else if (start > block->scan_hint_start ||
			   block->contig_hint > block->scan_hint) {
			
			block->scan_hint_start = start;
			block->scan_hint = contig;
		}
	} else {
		
		if ((start < block->contig_hint_start &&
		     (contig > block->scan_hint ||
		      (contig == block->scan_hint &&
		       start > block->scan_hint_start)))) {
			block->scan_hint_start = start;
			block->scan_hint = contig;
		}
	}
}

static void pcpu_block_update_scan(struct pcpu_chunk *chunk, int bit_off,
				   int bits)
{
	int s_off = pcpu_off_to_block_off(bit_off);
	int e_off = s_off + bits;
	int s_index, l_bit;
	struct pcpu_block_md *block;

	if (e_off > PCPU_BITMAP_BLOCK_BITS)
		return;

	s_index = pcpu_off_to_block_index(bit_off);
	block = chunk->md_blocks + s_index;

	
	l_bit = find_last_bit(pcpu_index_alloc_map(chunk, s_index), s_off);
	s_off = (s_off == l_bit) ? 0 : l_bit + 1;

	pcpu_block_update(block, s_off, e_off);
}

static void pcpu_chunk_refresh_hint(struct pcpu_chunk *chunk, bool full_scan)
{
	struct pcpu_block_md *chunk_md = &chunk->chunk_md;
	int bit_off, bits;

	
	if (!full_scan && chunk_md->scan_hint) {
		bit_off = chunk_md->scan_hint_start + chunk_md->scan_hint;
		chunk_md->contig_hint_start = chunk_md->scan_hint_start;
		chunk_md->contig_hint = chunk_md->scan_hint;
		chunk_md->scan_hint = 0;
	} else {
		bit_off = chunk_md->first_free;
		chunk_md->contig_hint = 0;
	}

	bits = 0;
	pcpu_for_each_md_free_region(chunk, bit_off, bits)
		pcpu_block_update(chunk_md, bit_off, bit_off + bits);
}

static void pcpu_block_refresh_hint(struct pcpu_chunk *chunk, int index)
{
	struct pcpu_block_md *block = chunk->md_blocks + index;
	unsigned long *alloc_map = pcpu_index_alloc_map(chunk, index);
	unsigned int start, end;	

	
	if (block->scan_hint) {
		start = block->scan_hint_start + block->scan_hint;
		block->contig_hint_start = block->scan_hint_start;
		block->contig_hint = block->scan_hint;
		block->scan_hint = 0;
	} else {
		start = block->first_free;
		block->contig_hint = 0;
	}

	block->right_free = 0;

	
	for_each_clear_bitrange_from(start, end, alloc_map, PCPU_BITMAP_BLOCK_BITS)
		pcpu_block_update(block, start, end);
}

static void pcpu_block_update_hint_alloc(struct pcpu_chunk *chunk, int bit_off,
					 int bits)
{
	struct pcpu_block_md *chunk_md = &chunk->chunk_md;
	int nr_empty_pages = 0;
	struct pcpu_block_md *s_block, *e_block, *block;
	int s_index, e_index;	
	int s_off, e_off;	

	
	s_index = pcpu_off_to_block_index(bit_off);
	e_index = pcpu_off_to_block_index(bit_off + bits - 1);
	s_off = pcpu_off_to_block_off(bit_off);
	e_off = pcpu_off_to_block_off(bit_off + bits - 1) + 1;

	s_block = chunk->md_blocks + s_index;
	e_block = chunk->md_blocks + e_index;

	
	if (s_block->contig_hint == PCPU_BITMAP_BLOCK_BITS)
		nr_empty_pages++;

	if (s_off == s_block->first_free)
		s_block->first_free = find_next_zero_bit(
					pcpu_index_alloc_map(chunk, s_index),
					PCPU_BITMAP_BLOCK_BITS,
					s_off + bits);

	if (pcpu_region_overlap(s_block->scan_hint_start,
				s_block->scan_hint_start + s_block->scan_hint,
				s_off,
				s_off + bits))
		s_block->scan_hint = 0;

	if (pcpu_region_overlap(s_block->contig_hint_start,
				s_block->contig_hint_start +
				s_block->contig_hint,
				s_off,
				s_off + bits)) {
		
		if (!s_off)
			s_block->left_free = 0;
		pcpu_block_refresh_hint(chunk, s_index);
	} else {
		
		s_block->left_free = min(s_block->left_free, s_off);
		if (s_index == e_index)
			s_block->right_free = min_t(int, s_block->right_free,
					PCPU_BITMAP_BLOCK_BITS - e_off);
		else
			s_block->right_free = 0;
	}

	
	if (s_index != e_index) {
		if (e_block->contig_hint == PCPU_BITMAP_BLOCK_BITS)
			nr_empty_pages++;

		
		e_block->first_free = find_next_zero_bit(
				pcpu_index_alloc_map(chunk, e_index),
				PCPU_BITMAP_BLOCK_BITS, e_off);

		if (e_off == PCPU_BITMAP_BLOCK_BITS) {
			
			e_block++;
		} else {
			if (e_off > e_block->scan_hint_start)
				e_block->scan_hint = 0;

			e_block->left_free = 0;
			if (e_off > e_block->contig_hint_start) {
				
				pcpu_block_refresh_hint(chunk, e_index);
			} else {
				e_block->right_free =
					min_t(int, e_block->right_free,
					      PCPU_BITMAP_BLOCK_BITS - e_off);
			}
		}

		
		nr_empty_pages += (e_index - s_index - 1);
		for (block = s_block + 1; block < e_block; block++) {
			block->scan_hint = 0;
			block->contig_hint = 0;
			block->left_free = 0;
			block->right_free = 0;
		}
	}

	if (nr_empty_pages)
		pcpu_update_empty_pages(chunk, -nr_empty_pages);

	if (pcpu_region_overlap(chunk_md->scan_hint_start,
				chunk_md->scan_hint_start +
				chunk_md->scan_hint,
				bit_off,
				bit_off + bits))
		chunk_md->scan_hint = 0;

	
	if (pcpu_region_overlap(chunk_md->contig_hint_start,
				chunk_md->contig_hint_start +
				chunk_md->contig_hint,
				bit_off,
				bit_off + bits))
		pcpu_chunk_refresh_hint(chunk, false);
}

static void pcpu_block_update_hint_free(struct pcpu_chunk *chunk, int bit_off,
					int bits)
{
	int nr_empty_pages = 0;
	struct pcpu_block_md *s_block, *e_block, *block;
	int s_index, e_index;	
	int s_off, e_off;	
	int start, end;		

	
	s_index = pcpu_off_to_block_index(bit_off);
	e_index = pcpu_off_to_block_index(bit_off + bits - 1);
	s_off = pcpu_off_to_block_off(bit_off);
	e_off = pcpu_off_to_block_off(bit_off + bits - 1) + 1;

	s_block = chunk->md_blocks + s_index;
	e_block = chunk->md_blocks + e_index;

	
	start = s_off;
	if (s_off == s_block->contig_hint + s_block->contig_hint_start) {
		start = s_block->contig_hint_start;
	} else {
		
		int l_bit = find_last_bit(pcpu_index_alloc_map(chunk, s_index),
					  start);
		start = (start == l_bit) ? 0 : l_bit + 1;
	}

	end = e_off;
	if (e_off == e_block->contig_hint_start)
		end = e_block->contig_hint_start + e_block->contig_hint;
	else
		end = find_next_bit(pcpu_index_alloc_map(chunk, e_index),
				    PCPU_BITMAP_BLOCK_BITS, end);

	
	e_off = (s_index == e_index) ? end : PCPU_BITMAP_BLOCK_BITS;
	if (!start && e_off == PCPU_BITMAP_BLOCK_BITS)
		nr_empty_pages++;
	pcpu_block_update(s_block, start, e_off);

	
	if (s_index != e_index) {
		
		if (end == PCPU_BITMAP_BLOCK_BITS)
			nr_empty_pages++;
		pcpu_block_update(e_block, 0, end);

		
		nr_empty_pages += (e_index - s_index - 1);
		for (block = s_block + 1; block < e_block; block++) {
			block->first_free = 0;
			block->scan_hint = 0;
			block->contig_hint_start = 0;
			block->contig_hint = PCPU_BITMAP_BLOCK_BITS;
			block->left_free = PCPU_BITMAP_BLOCK_BITS;
			block->right_free = PCPU_BITMAP_BLOCK_BITS;
		}
	}

	if (nr_empty_pages)
		pcpu_update_empty_pages(chunk, nr_empty_pages);

	
	if (((end - start) >= PCPU_BITMAP_BLOCK_BITS) || s_index != e_index)
		pcpu_chunk_refresh_hint(chunk, true);
	else
		pcpu_block_update(&chunk->chunk_md,
				  pcpu_block_off_to_off(s_index, start),
				  end);
}

static bool pcpu_is_populated(struct pcpu_chunk *chunk, int bit_off, int bits,
			      int *next_off)
{
	unsigned int start, end;

	start = PFN_DOWN(bit_off * PCPU_MIN_ALLOC_SIZE);
	end = PFN_UP((bit_off + bits) * PCPU_MIN_ALLOC_SIZE);

	start = find_next_zero_bit(chunk->populated, end, start);
	if (start >= end)
		return true;

	end = find_next_bit(chunk->populated, end, start + 1);

	*next_off = end * PAGE_SIZE / PCPU_MIN_ALLOC_SIZE;
	return false;
}

static int pcpu_find_block_fit(struct pcpu_chunk *chunk, int alloc_bits,
			       size_t align, bool pop_only)
{
	struct pcpu_block_md *chunk_md = &chunk->chunk_md;
	int bit_off, bits, next_off;

	
	if (!pcpu_check_block_hint(chunk_md, alloc_bits, align))
		return -1;

	bit_off = pcpu_next_hint(chunk_md, alloc_bits);
	bits = 0;
	pcpu_for_each_fit_region(chunk, alloc_bits, align, bit_off, bits) {
		if (!pop_only || pcpu_is_populated(chunk, bit_off, bits,
						   &next_off))
			break;

		bit_off = next_off;
		bits = 0;
	}

	if (bit_off == pcpu_chunk_map_bits(chunk))
		return -1;

	return bit_off;
}

static unsigned long pcpu_find_zero_area(unsigned long *map,
					 unsigned long size,
					 unsigned long start,
					 unsigned long nr,
					 unsigned long align_mask,
					 unsigned long *largest_off,
					 unsigned long *largest_bits)
{
	unsigned long index, end, i, area_off, area_bits;
again:
	index = find_next_zero_bit(map, size, start);

	
	index = __ALIGN_MASK(index, align_mask);
	area_off = index;

	end = index + nr;
	if (end > size)
		return end;
	i = find_next_bit(map, end, index);
	if (i < end) {
		area_bits = i - area_off;
		
		if (area_bits > *largest_bits ||
		    (area_bits == *largest_bits && *largest_off &&
		     (!area_off || __ffs(area_off) > __ffs(*largest_off)))) {
			*largest_off = area_off;
			*largest_bits = area_bits;
		}

		start = i + 1;
		goto again;
	}
	return index;
}

static int pcpu_alloc_area(struct pcpu_chunk *chunk, int alloc_bits,
			   size_t align, int start)
{
	struct pcpu_block_md *chunk_md = &chunk->chunk_md;
	size_t align_mask = (align) ? (align - 1) : 0;
	unsigned long area_off = 0, area_bits = 0;
	int bit_off, end, oslot;

	lockdep_assert_held(&pcpu_lock);

	oslot = pcpu_chunk_slot(chunk);

	
	end = min_t(int, start + alloc_bits + PCPU_BITMAP_BLOCK_BITS,
		    pcpu_chunk_map_bits(chunk));
	bit_off = pcpu_find_zero_area(chunk->alloc_map, end, start, alloc_bits,
				      align_mask, &area_off, &area_bits);
	if (bit_off >= end)
		return -1;

	if (area_bits)
		pcpu_block_update_scan(chunk, area_off, area_bits);

	
	bitmap_set(chunk->alloc_map, bit_off, alloc_bits);

	
	set_bit(bit_off, chunk->bound_map);
	bitmap_clear(chunk->bound_map, bit_off + 1, alloc_bits - 1);
	set_bit(bit_off + alloc_bits, chunk->bound_map);

	chunk->free_bytes -= alloc_bits * PCPU_MIN_ALLOC_SIZE;

	
	if (bit_off == chunk_md->first_free)
		chunk_md->first_free = find_next_zero_bit(
					chunk->alloc_map,
					pcpu_chunk_map_bits(chunk),
					bit_off + alloc_bits);

	pcpu_block_update_hint_alloc(chunk, bit_off, alloc_bits);

	pcpu_chunk_relocate(chunk, oslot);

	return bit_off * PCPU_MIN_ALLOC_SIZE;
}

static int pcpu_free_area(struct pcpu_chunk *chunk, int off)
{
	struct pcpu_block_md *chunk_md = &chunk->chunk_md;
	int bit_off, bits, end, oslot, freed;

	lockdep_assert_held(&pcpu_lock);
	pcpu_stats_area_dealloc(chunk);

	oslot = pcpu_chunk_slot(chunk);

	bit_off = off / PCPU_MIN_ALLOC_SIZE;

	
	end = find_next_bit(chunk->bound_map, pcpu_chunk_map_bits(chunk),
			    bit_off + 1);
	bits = end - bit_off;
	bitmap_clear(chunk->alloc_map, bit_off, bits);

	freed = bits * PCPU_MIN_ALLOC_SIZE;

	
	chunk->free_bytes += freed;

	
	chunk_md->first_free = min(chunk_md->first_free, bit_off);

	pcpu_block_update_hint_free(chunk, bit_off, bits);

	pcpu_chunk_relocate(chunk, oslot);

	return freed;
}

static void pcpu_init_md_block(struct pcpu_block_md *block, int nr_bits)
{
	block->scan_hint = 0;
	block->contig_hint = nr_bits;
	block->left_free = nr_bits;
	block->right_free = nr_bits;
	block->first_free = 0;
	block->nr_bits = nr_bits;
}

static void pcpu_init_md_blocks(struct pcpu_chunk *chunk)
{
	struct pcpu_block_md *md_block;

	
	pcpu_init_md_block(&chunk->chunk_md, pcpu_chunk_map_bits(chunk));

	for (md_block = chunk->md_blocks;
	     md_block != chunk->md_blocks + pcpu_chunk_nr_blocks(chunk);
	     md_block++)
		pcpu_init_md_block(md_block, PCPU_BITMAP_BLOCK_BITS);
}

static struct pcpu_chunk * __init pcpu_alloc_first_chunk(unsigned long tmp_addr,
							 int map_size)
{
	struct pcpu_chunk *chunk;
	unsigned long aligned_addr, lcm_align;
	int start_offset, offset_bits, region_size, region_bits;
	size_t alloc_size;

	
	aligned_addr = tmp_addr & PAGE_MASK;

	start_offset = tmp_addr - aligned_addr;

	
	lcm_align = lcm(PAGE_SIZE, PCPU_BITMAP_BLOCK_SIZE);
	region_size = ALIGN(start_offset + map_size, lcm_align);

	
	alloc_size = struct_size(chunk, populated,
				 BITS_TO_LONGS(region_size >> PAGE_SHIFT));
	chunk = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!chunk)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	INIT_LIST_HEAD(&chunk->list);

	chunk->base_addr = (void *)aligned_addr;
	chunk->start_offset = start_offset;
	chunk->end_offset = region_size - chunk->start_offset - map_size;

	chunk->nr_pages = region_size >> PAGE_SHIFT;
	region_bits = pcpu_chunk_map_bits(chunk);

	alloc_size = BITS_TO_LONGS(region_bits) * sizeof(chunk->alloc_map[0]);
	chunk->alloc_map = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!chunk->alloc_map)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	alloc_size =
		BITS_TO_LONGS(region_bits + 1) * sizeof(chunk->bound_map[0]);
	chunk->bound_map = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!chunk->bound_map)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	alloc_size = pcpu_chunk_nr_blocks(chunk) * sizeof(chunk->md_blocks[0]);
	chunk->md_blocks = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!chunk->md_blocks)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	pcpu_init_md_blocks(chunk);

	
	chunk->immutable = true;
	bitmap_fill(chunk->populated, chunk->nr_pages);
	chunk->nr_populated = chunk->nr_pages;
	chunk->nr_empty_pop_pages = chunk->nr_pages;

	chunk->free_bytes = map_size;

	if (chunk->start_offset) {
		
		offset_bits = chunk->start_offset / PCPU_MIN_ALLOC_SIZE;
		bitmap_set(chunk->alloc_map, 0, offset_bits);
		set_bit(0, chunk->bound_map);
		set_bit(offset_bits, chunk->bound_map);

		chunk->chunk_md.first_free = offset_bits;

		pcpu_block_update_hint_alloc(chunk, 0, offset_bits);
	}

	if (chunk->end_offset) {
		
		offset_bits = chunk->end_offset / PCPU_MIN_ALLOC_SIZE;
		bitmap_set(chunk->alloc_map,
			   pcpu_chunk_map_bits(chunk) - offset_bits,
			   offset_bits);
		set_bit((start_offset + map_size) / PCPU_MIN_ALLOC_SIZE,
			chunk->bound_map);
		set_bit(region_bits, chunk->bound_map);

		pcpu_block_update_hint_alloc(chunk, pcpu_chunk_map_bits(chunk)
					     - offset_bits, offset_bits);
	}

	return chunk;
}

static struct pcpu_chunk *pcpu_alloc_chunk(gfp_t gfp)
{
	struct pcpu_chunk *chunk;
	int region_bits;

	chunk = pcpu_mem_zalloc(pcpu_chunk_struct_size, gfp);
	if (!chunk)
		return NULL;

	INIT_LIST_HEAD(&chunk->list);
	chunk->nr_pages = pcpu_unit_pages;
	region_bits = pcpu_chunk_map_bits(chunk);

	chunk->alloc_map = pcpu_mem_zalloc(BITS_TO_LONGS(region_bits) *
					   sizeof(chunk->alloc_map[0]), gfp);
	if (!chunk->alloc_map)
		goto alloc_map_fail;

	chunk->bound_map = pcpu_mem_zalloc(BITS_TO_LONGS(region_bits + 1) *
					   sizeof(chunk->bound_map[0]), gfp);
	if (!chunk->bound_map)
		goto bound_map_fail;

	chunk->md_blocks = pcpu_mem_zalloc(pcpu_chunk_nr_blocks(chunk) *
					   sizeof(chunk->md_blocks[0]), gfp);
	if (!chunk->md_blocks)
		goto md_blocks_fail;

	pcpu_init_md_blocks(chunk);

	
	chunk->free_bytes = chunk->nr_pages * PAGE_SIZE;

	return chunk;

md_blocks_fail:
	pcpu_mem_free(chunk->bound_map);
bound_map_fail:
	pcpu_mem_free(chunk->alloc_map);
alloc_map_fail:
	pcpu_mem_free(chunk);

	return NULL;
}

static void pcpu_free_chunk(struct pcpu_chunk *chunk)
{
	if (!chunk)
		return;
	pcpu_mem_free(chunk->md_blocks);
	pcpu_mem_free(chunk->bound_map);
	pcpu_mem_free(chunk->alloc_map);
	pcpu_mem_free(chunk);
}

static void pcpu_chunk_populated(struct pcpu_chunk *chunk, int page_start,
				 int page_end)
{
	int nr = page_end - page_start;

	lockdep_assert_held(&pcpu_lock);

	bitmap_set(chunk->populated, page_start, nr);
	chunk->nr_populated += nr;
	pcpu_nr_populated += nr;

	pcpu_update_empty_pages(chunk, nr);
}

static void pcpu_chunk_depopulated(struct pcpu_chunk *chunk,
				   int page_start, int page_end)
{
	int nr = page_end - page_start;

	lockdep_assert_held(&pcpu_lock);

	bitmap_clear(chunk->populated, page_start, nr);
	chunk->nr_populated -= nr;
	pcpu_nr_populated -= nr;

	pcpu_update_empty_pages(chunk, -nr);
}

static int pcpu_populate_chunk(struct pcpu_chunk *chunk,
			       int page_start, int page_end, gfp_t gfp);
static void pcpu_depopulate_chunk(struct pcpu_chunk *chunk,
				  int page_start, int page_end);
static void pcpu_post_unmap_tlb_flush(struct pcpu_chunk *chunk,
				      int page_start, int page_end);
static struct pcpu_chunk *pcpu_create_chunk(gfp_t gfp);
static void pcpu_destroy_chunk(struct pcpu_chunk *chunk);
static struct page *pcpu_addr_to_page(void *addr);
static int __init pcpu_verify_alloc_info(const struct pcpu_alloc_info *ai);

#include "percpu-km.c"

static struct pcpu_chunk *pcpu_chunk_addr_search(void *addr)
{
	
	if (pcpu_addr_in_chunk(pcpu_first_chunk, addr))
		return pcpu_first_chunk;

	
	if (pcpu_addr_in_chunk(pcpu_reserved_chunk, addr))
		return pcpu_reserved_chunk;

	
	addr += pcpu_unit_offsets[raw_smp_processor_id()];
	return pcpu_get_page_chunk(pcpu_addr_to_page(addr));
}

static bool
pcpu_memcg_pre_alloc_hook(size_t size, gfp_t gfp, struct obj_cgroup **objcgp)
{
	return true;
}

static void pcpu_memcg_post_alloc_hook(struct obj_cgroup *objcg,
				       struct pcpu_chunk *chunk, int off,
				       size_t size)
{
}

static void pcpu_memcg_free_hook(struct pcpu_chunk *chunk, int off, size_t size)
{
}

static void __percpu *pcpu_alloc(size_t size, size_t align, bool reserved,
				 gfp_t gfp)
{
	gfp_t pcpu_gfp;
	bool is_atomic;
	bool do_warn;
	struct obj_cgroup *objcg = NULL;
	static int warn_limit = 10;
	struct pcpu_chunk *chunk, *next;
	const char *err;
	int slot, off, cpu, ret;
	unsigned long flags;
	void __percpu *ptr;
	size_t bits, bit_align;

	gfp = current_gfp_context(gfp);
	
	pcpu_gfp = gfp & (GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN);
	is_atomic = (gfp & GFP_KERNEL) != GFP_KERNEL;
	do_warn = !(gfp & __GFP_NOWARN);

	
	if (unlikely(align < PCPU_MIN_ALLOC_SIZE))
		align = PCPU_MIN_ALLOC_SIZE;

	size = ALIGN(size, PCPU_MIN_ALLOC_SIZE);
	bits = size >> PCPU_MIN_ALLOC_SHIFT;
	bit_align = align >> PCPU_MIN_ALLOC_SHIFT;

	if (unlikely(!size || size > PCPU_MIN_UNIT_SIZE || align > PAGE_SIZE ||
		     !is_power_of_2(align))) {
		WARN(do_warn, "illegal size (%zu) or align (%zu) for percpu allocation\n",
		     size, align);
		return NULL;
	}

	if (unlikely(!pcpu_memcg_pre_alloc_hook(size, gfp, &objcg)))
		return NULL;

	if (!is_atomic) {
		
		if (gfp & __GFP_NOFAIL) {
			mutex_lock(&pcpu_alloc_mutex);
		} else if (mutex_lock_killable(&pcpu_alloc_mutex)) {
			pcpu_memcg_post_alloc_hook(objcg, NULL, 0, size);
			return NULL;
		}
	}

	spin_lock_irqsave(&pcpu_lock, flags);

	
	if (reserved && pcpu_reserved_chunk) {
		chunk = pcpu_reserved_chunk;

		off = pcpu_find_block_fit(chunk, bits, bit_align, is_atomic);
		if (off < 0) {
			err = "alloc from reserved chunk failed";
			goto fail_unlock;
		}

		off = pcpu_alloc_area(chunk, bits, bit_align, off);
		if (off >= 0)
			goto area_found;

		err = "alloc from reserved chunk failed";
		goto fail_unlock;
	}

restart:
	
	for (slot = pcpu_size_to_slot(size); slot <= pcpu_free_slot; slot++) {
		list_for_each_entry_safe(chunk, next, &pcpu_chunk_lists[slot],
					 list) {
			off = pcpu_find_block_fit(chunk, bits, bit_align,
						  is_atomic);
			if (off < 0) {
				if (slot < PCPU_SLOT_FAIL_THRESHOLD)
					pcpu_chunk_move(chunk, 0);
				continue;
			}

			off = pcpu_alloc_area(chunk, bits, bit_align, off);
			if (off >= 0) {
				pcpu_reintegrate_chunk(chunk);
				goto area_found;
			}
		}
	}

	spin_unlock_irqrestore(&pcpu_lock, flags);

	
	if (is_atomic) {
		err = "atomic alloc failed, no space left";
		goto fail;
	}

	if (list_empty(&pcpu_chunk_lists[pcpu_free_slot])) {
		chunk = pcpu_create_chunk(pcpu_gfp);
		if (!chunk) {
			err = "failed to allocate new chunk";
			goto fail;
		}

		spin_lock_irqsave(&pcpu_lock, flags);
		pcpu_chunk_relocate(chunk, -1);
	} else {
		spin_lock_irqsave(&pcpu_lock, flags);
	}

	goto restart;

area_found:
	pcpu_stats_area_alloc(chunk, size);
	spin_unlock_irqrestore(&pcpu_lock, flags);

	
	if (!is_atomic) {
		unsigned int page_end, rs, re;

		rs = PFN_DOWN(off);
		page_end = PFN_UP(off + size);

		for_each_clear_bitrange_from(rs, re, chunk->populated, page_end) {
			WARN_ON(chunk->immutable);

			ret = pcpu_populate_chunk(chunk, rs, re, pcpu_gfp);

			spin_lock_irqsave(&pcpu_lock, flags);
			if (ret) {
				pcpu_free_area(chunk, off);
				err = "failed to populate";
				goto fail_unlock;
			}
			pcpu_chunk_populated(chunk, rs, re);
			spin_unlock_irqrestore(&pcpu_lock, flags);
		}

		mutex_unlock(&pcpu_alloc_mutex);
	}

	if (pcpu_nr_empty_pop_pages < PCPU_EMPTY_POP_PAGES_LOW)
		pcpu_schedule_balance_work();

	
	for_each_possible_cpu(cpu)
		memset((void *)pcpu_chunk_addr(chunk, cpu, 0) + off, 0, size);

	ptr = __addr_to_pcpu_ptr(chunk->base_addr + off);
	kmemleak_alloc_percpu(ptr, size, gfp);


	pcpu_memcg_post_alloc_hook(objcg, chunk, off, size);

	return ptr;

fail_unlock:
	spin_unlock_irqrestore(&pcpu_lock, flags);
fail:

	if (!is_atomic && do_warn && warn_limit) {
		pr_warn("allocation failed, size=%zu align=%zu atomic=%d, %s\n",
			size, align, is_atomic, err);
		dump_stack();
		if (!--warn_limit)
			pr_info("limit reached, disable warning\n");
	}
	if (is_atomic) {
		
		pcpu_atomic_alloc_failed = true;
		pcpu_schedule_balance_work();
	} else {
		mutex_unlock(&pcpu_alloc_mutex);
	}

	pcpu_memcg_post_alloc_hook(objcg, NULL, 0, size);

	return NULL;
}

void __percpu *__alloc_percpu_gfp(size_t size, size_t align, gfp_t gfp)
{
	return pcpu_alloc(size, align, false, gfp);
}

void __percpu *__alloc_percpu(size_t size, size_t align)
{
	return pcpu_alloc(size, align, false, GFP_KERNEL);
}

void __percpu *__alloc_reserved_percpu(size_t size, size_t align)
{
	return pcpu_alloc(size, align, true, GFP_KERNEL);
}

static void pcpu_balance_free(bool empty_only)
{
	LIST_HEAD(to_free);
	struct list_head *free_head = &pcpu_chunk_lists[pcpu_free_slot];
	struct pcpu_chunk *chunk, *next;

	lockdep_assert_held(&pcpu_lock);

	
	list_for_each_entry_safe(chunk, next, free_head, list) {
		WARN_ON(chunk->immutable);

		
		if (chunk == list_first_entry(free_head, struct pcpu_chunk, list))
			continue;

		if (!empty_only || chunk->nr_empty_pop_pages == 0)
			list_move(&chunk->list, &to_free);
	}

	if (list_empty(&to_free))
		return;

	spin_unlock_irq(&pcpu_lock);
	list_for_each_entry_safe(chunk, next, &to_free, list) {
		unsigned int rs, re;

		for_each_set_bitrange(rs, re, chunk->populated, chunk->nr_pages) {
			pcpu_depopulate_chunk(chunk, rs, re);
			spin_lock_irq(&pcpu_lock);
			pcpu_chunk_depopulated(chunk, rs, re);
			spin_unlock_irq(&pcpu_lock);
		}
		pcpu_destroy_chunk(chunk);
		cond_resched();
	}
	spin_lock_irq(&pcpu_lock);
}

static void pcpu_balance_populated(void)
{
	
	const gfp_t gfp = GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN;
	struct pcpu_chunk *chunk;
	int slot, nr_to_pop, ret;

	lockdep_assert_held(&pcpu_lock);

	
retry_pop:
	if (pcpu_atomic_alloc_failed) {
		nr_to_pop = PCPU_EMPTY_POP_PAGES_HIGH;
		
		pcpu_atomic_alloc_failed = false;
	} else {
		nr_to_pop = clamp(PCPU_EMPTY_POP_PAGES_HIGH -
				  pcpu_nr_empty_pop_pages,
				  0, PCPU_EMPTY_POP_PAGES_HIGH);
	}

	for (slot = pcpu_size_to_slot(PAGE_SIZE); slot <= pcpu_free_slot; slot++) {
		unsigned int nr_unpop = 0, rs, re;

		if (!nr_to_pop)
			break;

		list_for_each_entry(chunk, &pcpu_chunk_lists[slot], list) {
			nr_unpop = chunk->nr_pages - chunk->nr_populated;
			if (nr_unpop)
				break;
		}

		if (!nr_unpop)
			continue;

		
		for_each_clear_bitrange(rs, re, chunk->populated, chunk->nr_pages) {
			int nr = min_t(int, re - rs, nr_to_pop);

			spin_unlock_irq(&pcpu_lock);
			ret = pcpu_populate_chunk(chunk, rs, rs + nr, gfp);
			cond_resched();
			spin_lock_irq(&pcpu_lock);
			if (!ret) {
				nr_to_pop -= nr;
				pcpu_chunk_populated(chunk, rs, rs + nr);
			} else {
				nr_to_pop = 0;
			}

			if (!nr_to_pop)
				break;
		}
	}

	if (nr_to_pop) {
		
		spin_unlock_irq(&pcpu_lock);
		chunk = pcpu_create_chunk(gfp);
		cond_resched();
		spin_lock_irq(&pcpu_lock);
		if (chunk) {
			pcpu_chunk_relocate(chunk, -1);
			goto retry_pop;
		}
	}
}

static void pcpu_reclaim_populated(void)
{
	struct pcpu_chunk *chunk;
	struct pcpu_block_md *block;
	int freed_page_start, freed_page_end;
	int i, end;
	bool reintegrate;

	lockdep_assert_held(&pcpu_lock);

	
	while (!list_empty(&pcpu_chunk_lists[pcpu_to_depopulate_slot])) {
		chunk = list_first_entry(&pcpu_chunk_lists[pcpu_to_depopulate_slot],
					 struct pcpu_chunk, list);
		WARN_ON(chunk->immutable);

		
		freed_page_start = chunk->nr_pages;
		freed_page_end = 0;
		reintegrate = false;
		for (i = chunk->nr_pages - 1, end = -1; i >= 0; i--) {
			
			if (chunk->nr_empty_pop_pages == 0)
				break;

			
			if (pcpu_nr_empty_pop_pages < PCPU_EMPTY_POP_PAGES_HIGH) {
				reintegrate = true;
				goto end_chunk;
			}

			
			block = chunk->md_blocks + i;
			if (block->contig_hint == PCPU_BITMAP_BLOCK_BITS &&
			    test_bit(i, chunk->populated)) {
				if (end == -1)
					end = i;
				if (i > 0)
					continue;
				i--;
			}

			
			if (end == -1)
				continue;

			spin_unlock_irq(&pcpu_lock);
			pcpu_depopulate_chunk(chunk, i + 1, end + 1);
			cond_resched();
			spin_lock_irq(&pcpu_lock);

			pcpu_chunk_depopulated(chunk, i + 1, end + 1);
			freed_page_start = min(freed_page_start, i + 1);
			freed_page_end = max(freed_page_end, end + 1);

			
			end = -1;
		}

end_chunk:
		
		if (freed_page_start < freed_page_end) {
			spin_unlock_irq(&pcpu_lock);
			pcpu_post_unmap_tlb_flush(chunk,
						  freed_page_start,
						  freed_page_end);
			cond_resched();
			spin_lock_irq(&pcpu_lock);
		}

		if (reintegrate || chunk->free_bytes == pcpu_unit_size)
			pcpu_reintegrate_chunk(chunk);
		else
			list_move_tail(&chunk->list,
				       &pcpu_chunk_lists[pcpu_sidelined_slot]);
	}
}

static void pcpu_balance_workfn(struct work_struct *work)
{
	
	mutex_lock(&pcpu_alloc_mutex);
	spin_lock_irq(&pcpu_lock);

	pcpu_balance_free(false);
	pcpu_reclaim_populated();
	pcpu_balance_populated();
	pcpu_balance_free(true);

	spin_unlock_irq(&pcpu_lock);
	mutex_unlock(&pcpu_alloc_mutex);
}

void free_percpu(void __percpu *ptr)
{
	void *addr;
	struct pcpu_chunk *chunk;
	unsigned long flags;
	int size, off;
	bool need_balance = false;

	if (!ptr)
		return;

	kmemleak_free_percpu(ptr);

	addr = __pcpu_ptr_to_addr(ptr);

	spin_lock_irqsave(&pcpu_lock, flags);

	chunk = pcpu_chunk_addr_search(addr);
	off = addr - chunk->base_addr;

	size = pcpu_free_area(chunk, off);

	pcpu_memcg_free_hook(chunk, off, size);

	
	if (!chunk->isolated && chunk->free_bytes == pcpu_unit_size) {
		struct pcpu_chunk *pos;

		list_for_each_entry(pos, &pcpu_chunk_lists[pcpu_free_slot], list)
			if (pos != chunk) {
				need_balance = true;
				break;
			}
	} else if (pcpu_should_reclaim_chunk(chunk)) {
		pcpu_isolate_chunk(chunk);
		need_balance = true;
	}


	spin_unlock_irqrestore(&pcpu_lock, flags);

	if (need_balance)
		pcpu_schedule_balance_work();
}

bool __is_kernel_percpu_address(unsigned long addr, unsigned long *can_addr)
{
	
	return false;
}

bool is_kernel_percpu_address(unsigned long addr)
{
	return __is_kernel_percpu_address(addr, NULL);
}

phys_addr_t per_cpu_ptr_to_phys(void *addr)
{
	void __percpu *base = __addr_to_pcpu_ptr(pcpu_base_addr);
	bool in_first_chunk = false;
	unsigned long first_low, first_high;
	unsigned int cpu;

	
	first_low = (unsigned long)pcpu_base_addr +
		    pcpu_unit_page_offset(pcpu_low_unit_cpu, 0);
	first_high = (unsigned long)pcpu_base_addr +
		     pcpu_unit_page_offset(pcpu_high_unit_cpu, pcpu_unit_pages);
	if ((unsigned long)addr >= first_low &&
	    (unsigned long)addr < first_high) {
		for_each_possible_cpu(cpu) {
			void *start = per_cpu_ptr(base, cpu);

			if (addr >= start && addr < start + pcpu_unit_size) {
				in_first_chunk = true;
				break;
			}
		}
	}

	if (in_first_chunk) {
		if (!is_vmalloc_addr(addr))
			return __pa(addr);
		else
			return page_to_phys(vmalloc_to_page(addr)) +
			       offset_in_page(addr);
	} else
		return page_to_phys(pcpu_addr_to_page(addr)) +
		       offset_in_page(addr);
}

struct pcpu_alloc_info * __init pcpu_alloc_alloc_info(int nr_groups,
						      int nr_units)
{
	struct pcpu_alloc_info *ai;
	size_t base_size, ai_size;
	void *ptr;
	int unit;

	base_size = ALIGN(struct_size(ai, groups, nr_groups),
			  __alignof__(ai->groups[0].cpu_map[0]));
	ai_size = base_size + nr_units * sizeof(ai->groups[0].cpu_map[0]);

	ptr = memblock_alloc(PFN_ALIGN(ai_size), PAGE_SIZE);
	if (!ptr)
		return NULL;
	ai = ptr;
	ptr += base_size;

	ai->groups[0].cpu_map = ptr;

	for (unit = 0; unit < nr_units; unit++)
		ai->groups[0].cpu_map[unit] = NR_CPUS;

	ai->nr_groups = nr_groups;
	ai->__ai_size = PFN_ALIGN(ai_size);

	return ai;
}

void __init pcpu_free_alloc_info(struct pcpu_alloc_info *ai)
{
	memblock_free(ai, ai->__ai_size);
}

static void pcpu_dump_alloc_info(const char *lvl,
				 const struct pcpu_alloc_info *ai)
{
	int group_width = 1, cpu_width = 1, width;
	char empty_str[] = "--------";
	int alloc = 0, alloc_end = 0;
	int group, v;
	int upa, apl;	

	v = ai->nr_groups;
	while (v /= 10)
		group_width++;

	v = num_possible_cpus();
	while (v /= 10)
		cpu_width++;
	empty_str[min_t(int, cpu_width, sizeof(empty_str) - 1)] = '\0';

	upa = ai->alloc_size / ai->unit_size;
	width = upa * (cpu_width + 1) + group_width + 3;
	apl = rounddown_pow_of_two(max(60 / width, 1));

	printk("%spcpu-alloc: s%zu r%zu d%zu u%zu alloc=%zu*%zu",
	       lvl, ai->static_size, ai->reserved_size, ai->dyn_size,
	       ai->unit_size, ai->alloc_size / ai->atom_size, ai->atom_size);

	for (group = 0; group < ai->nr_groups; group++) {
		const struct pcpu_group_info *gi = &ai->groups[group];
		int unit = 0, unit_end = 0;

		BUG_ON(gi->nr_units % upa);
		for (alloc_end += gi->nr_units / upa;
		     alloc < alloc_end; alloc++) {
			if (!(alloc % apl)) {
				pr_cont("\n");
				printk("%spcpu-alloc: ", lvl);
			}
			pr_cont("[%0*d] ", group_width, group);

			for (unit_end += upa; unit < unit_end; unit++)
				if (gi->cpu_map[unit] != NR_CPUS)
					pr_cont("%0*d ",
						cpu_width, gi->cpu_map[unit]);
				else
					pr_cont("%s ", empty_str);
		}
	}
	pr_cont("\n");
}

void __init pcpu_setup_first_chunk(const struct pcpu_alloc_info *ai,
				   void *base_addr)
{
	size_t size_sum = ai->static_size + ai->reserved_size + ai->dyn_size;
	size_t static_size, dyn_size;
	struct pcpu_chunk *chunk;
	unsigned long *group_offsets;
	size_t *group_sizes;
	unsigned long *unit_off;
	unsigned int cpu;
	int *unit_map;
	int group, unit, i;
	int map_size;
	unsigned long tmp_addr;
	size_t alloc_size;

#define PCPU_SETUP_BUG_ON(cond)	do {					\
	if (unlikely(cond)) {						\
		pr_emerg("failed to initialize, %s\n", #cond);		\
		pr_emerg("cpu_possible_mask=%*pb\n",			\
			 cpumask_pr_args(cpu_possible_mask));		\
		pcpu_dump_alloc_info(KERN_EMERG, ai);			\
		BUG();							\
	}								\
} while (0)

	
	PCPU_SETUP_BUG_ON(ai->nr_groups <= 0);
	PCPU_SETUP_BUG_ON(!base_addr);
	PCPU_SETUP_BUG_ON(offset_in_page(base_addr));
	PCPU_SETUP_BUG_ON(ai->unit_size < size_sum);
	PCPU_SETUP_BUG_ON(offset_in_page(ai->unit_size));
	PCPU_SETUP_BUG_ON(ai->unit_size < PCPU_MIN_UNIT_SIZE);
	PCPU_SETUP_BUG_ON(!IS_ALIGNED(ai->unit_size, PCPU_BITMAP_BLOCK_SIZE));
	PCPU_SETUP_BUG_ON(ai->dyn_size < PERCPU_DYNAMIC_EARLY_SIZE);
	PCPU_SETUP_BUG_ON(!ai->dyn_size);
	PCPU_SETUP_BUG_ON(!IS_ALIGNED(ai->reserved_size, PCPU_MIN_ALLOC_SIZE));
	PCPU_SETUP_BUG_ON(!(IS_ALIGNED(PCPU_BITMAP_BLOCK_SIZE, PAGE_SIZE) ||
			    IS_ALIGNED(PAGE_SIZE, PCPU_BITMAP_BLOCK_SIZE)));
	PCPU_SETUP_BUG_ON(pcpu_verify_alloc_info(ai) < 0);

	
	alloc_size = ai->nr_groups * sizeof(group_offsets[0]);
	group_offsets = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!group_offsets)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	alloc_size = ai->nr_groups * sizeof(group_sizes[0]);
	group_sizes = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!group_sizes)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	alloc_size = nr_cpu_ids * sizeof(unit_map[0]);
	unit_map = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!unit_map)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	alloc_size = nr_cpu_ids * sizeof(unit_off[0]);
	unit_off = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!unit_off)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	for (cpu = 0; cpu < nr_cpu_ids; cpu++)
		unit_map[cpu] = UINT_MAX;

	pcpu_low_unit_cpu = NR_CPUS;
	pcpu_high_unit_cpu = NR_CPUS;

	for (group = 0, unit = 0; group < ai->nr_groups; group++, unit += i) {
		const struct pcpu_group_info *gi = &ai->groups[group];

		group_offsets[group] = gi->base_offset;
		group_sizes[group] = gi->nr_units * ai->unit_size;

		for (i = 0; i < gi->nr_units; i++) {
			cpu = gi->cpu_map[i];
			if (cpu == NR_CPUS)
				continue;

			PCPU_SETUP_BUG_ON(cpu >= nr_cpu_ids);
			PCPU_SETUP_BUG_ON(!cpu_possible(cpu));
			PCPU_SETUP_BUG_ON(unit_map[cpu] != UINT_MAX);

			unit_map[cpu] = unit + i;
			unit_off[cpu] = gi->base_offset + i * ai->unit_size;

			
			if (pcpu_low_unit_cpu == NR_CPUS ||
			    unit_off[cpu] < unit_off[pcpu_low_unit_cpu])
				pcpu_low_unit_cpu = cpu;
			if (pcpu_high_unit_cpu == NR_CPUS ||
			    unit_off[cpu] > unit_off[pcpu_high_unit_cpu])
				pcpu_high_unit_cpu = cpu;
		}
	}
	pcpu_nr_units = unit;

	for_each_possible_cpu(cpu)
		PCPU_SETUP_BUG_ON(unit_map[cpu] == UINT_MAX);

	
#undef PCPU_SETUP_BUG_ON
	pcpu_dump_alloc_info(KERN_DEBUG, ai);

	pcpu_nr_groups = ai->nr_groups;
	pcpu_group_offsets = group_offsets;
	pcpu_group_sizes = group_sizes;
	pcpu_unit_map = unit_map;
	pcpu_unit_offsets = unit_off;

	
	pcpu_unit_pages = ai->unit_size >> PAGE_SHIFT;
	pcpu_unit_size = pcpu_unit_pages << PAGE_SHIFT;
	pcpu_atom_size = ai->atom_size;
	pcpu_chunk_struct_size = struct_size(chunk, populated,
					     BITS_TO_LONGS(pcpu_unit_pages));

	pcpu_stats_save_ai(ai);

	
	pcpu_sidelined_slot = __pcpu_size_to_slot(pcpu_unit_size) + 1;
	pcpu_free_slot = pcpu_sidelined_slot + 1;
	pcpu_to_depopulate_slot = pcpu_free_slot + 1;
	pcpu_nr_slots = pcpu_to_depopulate_slot + 1;
	pcpu_chunk_lists = memblock_alloc(pcpu_nr_slots *
					  sizeof(pcpu_chunk_lists[0]),
					  SMP_CACHE_BYTES);
	if (!pcpu_chunk_lists)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      pcpu_nr_slots * sizeof(pcpu_chunk_lists[0]));

	for (i = 0; i < pcpu_nr_slots; i++)
		INIT_LIST_HEAD(&pcpu_chunk_lists[i]);

	
	static_size = ALIGN(ai->static_size, PCPU_MIN_ALLOC_SIZE);
	dyn_size = ai->dyn_size - (static_size - ai->static_size);

	
	tmp_addr = (unsigned long)base_addr + static_size;
	map_size = ai->reserved_size ?: dyn_size;
	chunk = pcpu_alloc_first_chunk(tmp_addr, map_size);

	
	if (ai->reserved_size) {
		pcpu_reserved_chunk = chunk;

		tmp_addr = (unsigned long)base_addr + static_size +
			   ai->reserved_size;
		map_size = dyn_size;
		chunk = pcpu_alloc_first_chunk(tmp_addr, map_size);
	}

	
	pcpu_first_chunk = chunk;
	pcpu_nr_empty_pop_pages = pcpu_first_chunk->nr_empty_pop_pages;
	pcpu_chunk_relocate(pcpu_first_chunk, -1);

	
	pcpu_nr_populated += PFN_DOWN(size_sum);

	pcpu_stats_chunk_alloc();

	
	pcpu_base_addr = base_addr;
}

void __init setup_per_cpu_areas(void)
{
	const size_t unit_size =
		roundup_pow_of_two(max_t(size_t, PCPU_MIN_UNIT_SIZE,
					 PERCPU_DYNAMIC_RESERVE));
	struct pcpu_alloc_info *ai;
	void *fc;

	ai = pcpu_alloc_alloc_info(1, 1);
	fc = memblock_alloc_from(unit_size, PAGE_SIZE, __pa(MAX_DMA_ADDRESS));
	if (!ai || !fc)
		panic("Failed to allocate memory for percpu areas.");
	
	kmemleak_free(fc);

	ai->dyn_size = unit_size;
	ai->unit_size = unit_size;
	ai->atom_size = unit_size;
	ai->alloc_size = unit_size;
	ai->groups[0].nr_units = 1;
	ai->groups[0].cpu_map[0] = 0;

	pcpu_setup_first_chunk(ai, fc);
	pcpu_free_alloc_info(ai);
}

unsigned long pcpu_nr_pages(void)
{
	return pcpu_nr_populated * pcpu_nr_units;
}

static int __init percpu_enable_async(void)
{
	pcpu_async_enabled = true;
	return 0;
}
subsys_initcall(percpu_enable_async);
