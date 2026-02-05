
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitmap.h>
#include <linux/cpumask.h>
#include <linux/memblock.h>
#include <linux/err.h>
unsigned long lcm(unsigned long a, unsigned long b) __attribute_const__;
/* lcm_not_zero declaration removed - never called */
#include <linux/list.h>
#include <linux/log2.h>
#include <linux/mm.h>
/* linux/module.h removed - no module features used */
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/pfn.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
/* linux/memcontrol.h removed - memcg hooks are empty stubs */

#include <asm/cacheflush.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>

struct pcpu_block_md {
	int scan_hint;
	int scan_hint_start;
	int contig_hint;
	int contig_hint_start;
	int left_free;
	int right_free;
	int first_free;
	int nr_bits;
};

struct pcpu_chunk {
	struct list_head list;
	int free_bytes;
	struct pcpu_block_md chunk_md;
	void *base_addr;
	unsigned long *alloc_map;
	unsigned long *bound_map;
	struct pcpu_block_md *md_blocks;
	void *data;
	bool immutable;
	bool isolated;
	int start_offset;
	int end_offset;
	int nr_pages;
	int nr_populated;
	int nr_empty_pop_pages;
	unsigned long populated[];
};

/* Redundant extern declarations removed - all defined in same file below */

static inline int pcpu_chunk_nr_blocks(struct pcpu_chunk *chunk)
{
	return chunk->nr_pages * PAGE_SIZE / PCPU_BITMAP_BLOCK_SIZE;
}

/* pcpu_nr_pages_to_map_bits inlined */
static inline int pcpu_chunk_map_bits(struct pcpu_chunk *chunk)
{
	return chunk->nr_pages * PAGE_SIZE / PCPU_MIN_ALLOC_SIZE;
}

/* pcpu_stats_* stubs removed - statistics tracking disabled */
/* end percpu-internal.h */

#define PCPU_SLOT_BASE_SHIFT 5

#define PCPU_SLOT_FAIL_THRESHOLD 3

/* PCPU_EMPTY_POP_PAGES_LOW, PCPU_EMPTY_POP_PAGES_HIGH removed - never used */

#define __addr_to_pcpu_ptr(addr) (void __percpu *)(addr)
#define __pcpu_ptr_to_addr(ptr) (void __force *)(ptr)

static int pcpu_unit_pages __ro_after_init;
static int pcpu_unit_size __ro_after_init;
/* pcpu_nr_units removed - only written, never read */
/* pcpu_atom_size removed - only written, never read */
int pcpu_nr_slots __ro_after_init;
static int pcpu_free_slot __ro_after_init;
int pcpu_sidelined_slot __ro_after_init;
int pcpu_to_depopulate_slot __ro_after_init;
static size_t pcpu_chunk_struct_size __ro_after_init;

/* pcpu_low_unit_cpu, pcpu_high_unit_cpu removed - only written, never read */

/* pcpu_unit_map removed - only written, never read */
const unsigned long *pcpu_unit_offsets __ro_after_init;

/* pcpu_nr_groups, pcpu_group_offsets removed - only written, never read */
static const size_t *pcpu_group_sizes __ro_after_init;

struct pcpu_chunk *pcpu_first_chunk __ro_after_init;

struct pcpu_chunk *pcpu_reserved_chunk __ro_after_init;

DEFINE_SPINLOCK(pcpu_lock);
static DEFINE_MUTEX(pcpu_alloc_mutex);

struct list_head *pcpu_chunk_lists __ro_after_init;

/* pcpu_nr_empty_pop_pages removed - write-only variable, never read */

/* pcpu_nr_populated removed - only written, never read */
/* pcpu_balance_work, pcpu_async_enabled removed - workfn was a no-op stub */
/* pcpu_atomic_alloc_failed removed - is_atomic always false, never set */

/* pcpu_schedule_balance_work inlined */

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

/* pcpu_set_page_chunk inlined into pcpu_create_chunk
 * pcpu_unit_page_offset inlined into pcpu_chunk_addr
 * pcpu_chunk_addr inlined into pcpu_alloc */

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

/* pcpu_check_block_hint inlined */

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
			*bit_off = pcpu_block_off_to_off(
				i, block->contig_hint_start);
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

			*bits += alloc_bits + block->contig_hint_start - start;
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

#define pcpu_for_each_md_free_region(chunk, bit_off, bits)           \
	for (pcpu_next_md_free_region((chunk), &(bit_off), &(bits)); \
	     (bit_off) < pcpu_chunk_map_bits((chunk));               \
	     (bit_off) += (bits) + 1,                                \
	     pcpu_next_md_free_region((chunk), &(bit_off), &(bits)))

#define pcpu_for_each_fit_region(chunk, alloc_bits, align, bit_off, bits)     \
	for (pcpu_next_fit_region((chunk), (alloc_bits), (align), &(bit_off), \
				  &(bits));                                   \
	     (bit_off) < pcpu_chunk_map_bits((chunk));                        \
	     (bit_off) += (bits),                                             \
	     pcpu_next_fit_region((chunk), (alloc_bits), (align), &(bit_off), \
				  &(bits)))

static void *pcpu_mem_zalloc(size_t size, gfp_t gfp)
{
	if (WARN_ON_ONCE(!slab_is_available()))
		return NULL;

	if (size <= PAGE_SIZE)
		return kzalloc(size, gfp);
	else
		return __vmalloc_node(size, 1, gfp | __GFP_ZERO, NUMA_NO_NODE,
				      __builtin_return_address(0));
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

static void pcpu_chunk_relocate(struct pcpu_chunk *chunk, int oslot)
{
	int nslot = pcpu_chunk_slot(chunk);

	if (chunk->isolated)
		return;

	if (oslot != nslot)
		__pcpu_chunk_move(chunk, nslot, oslot < nslot);
}

/* Removed: pcpu_isolate_chunk - dead code since free_percpu is a no-op */
/* pcpu_reintegrate_chunk inlined into pcpu_alloc - single caller */

static inline void pcpu_update_empty_pages(struct pcpu_chunk *chunk, int nr)
{
	chunk->nr_empty_pop_pages += nr;
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

/* pcpu_block_update_scan, pcpu_chunk_refresh_hint inlined into pcpu_alloc_area */

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

	for_each_clear_bitrange_from(start, end, alloc_map,
				     PCPU_BITMAP_BLOCK_BITS)
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
			PCPU_BITMAP_BLOCK_BITS, s_off + bits);

	if (pcpu_region_overlap(s_block->scan_hint_start,
				s_block->scan_hint_start + s_block->scan_hint,
				s_off, s_off + bits))
		s_block->scan_hint = 0;

	if (pcpu_region_overlap(s_block->contig_hint_start,
				s_block->contig_hint_start +
					s_block->contig_hint,
				s_off, s_off + bits)) {
		if (!s_off)
			s_block->left_free = 0;
		pcpu_block_refresh_hint(chunk, s_index);
	} else {
		s_block->left_free = min(s_block->left_free, s_off);
		if (s_index == e_index)
			s_block->right_free =
				min_t(int, s_block->right_free,
				      PCPU_BITMAP_BLOCK_BITS - e_off);
		else
			s_block->right_free = 0;
	}

	if (s_index != e_index) {
		if (e_block->contig_hint == PCPU_BITMAP_BLOCK_BITS)
			nr_empty_pages++;

		e_block->first_free =
			find_next_zero_bit(pcpu_index_alloc_map(chunk, e_index),
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
				chunk_md->scan_hint_start + chunk_md->scan_hint,
				bit_off, bit_off + bits))
		chunk_md->scan_hint = 0;

	if (pcpu_region_overlap(chunk_md->contig_hint_start,
				chunk_md->contig_hint_start +
					chunk_md->contig_hint,
				bit_off, bit_off + bits)) {
		/* inlined pcpu_chunk_refresh_hint */
		int refresh_bit_off, refresh_bits;
		if (chunk_md->scan_hint) {
			refresh_bit_off =
				chunk_md->scan_hint_start + chunk_md->scan_hint;
			chunk_md->contig_hint_start = chunk_md->scan_hint_start;
			chunk_md->contig_hint = chunk_md->scan_hint;
			chunk_md->scan_hint = 0;
		} else {
			refresh_bit_off = chunk_md->first_free;
			chunk_md->contig_hint = 0;
		}
		refresh_bits = 0;
		pcpu_for_each_md_free_region(chunk, refresh_bit_off,
					     refresh_bits)
			pcpu_block_update(chunk_md, refresh_bit_off,
					  refresh_bit_off + refresh_bits);
	}
}

/* pcpu_is_populated removed - only called when pop_only was true, which was always false */
/* pop_only parameter removed - always false, so condition always breaks immediately */
static int pcpu_find_block_fit(struct pcpu_chunk *chunk, int alloc_bits,
			       size_t align)
{
	struct pcpu_block_md *chunk_md = &chunk->chunk_md;
	int bit_off, bits;
	int hint_off = ALIGN(chunk_md->contig_hint_start, align) -
		       chunk_md->contig_hint_start;

	if (hint_off + alloc_bits > chunk_md->contig_hint)
		return -1;

	bit_off = pcpu_next_hint(chunk_md, alloc_bits);
	bits = 0;
	pcpu_for_each_fit_region(
		chunk, alloc_bits, align, bit_off,
		bits) break; /* pop_only was always false, so !pop_only always true */

	if (bit_off == pcpu_chunk_map_bits(chunk))
		return -1;

	return bit_off;
}

/* pcpu_find_zero_area inlined into pcpu_alloc_area */

static int pcpu_alloc_area(struct pcpu_chunk *chunk, int alloc_bits,
			   size_t align, int start)
{
	struct pcpu_block_md *chunk_md = &chunk->chunk_md;
	size_t align_mask = (align) ? (align - 1) : 0;
	unsigned long area_off = 0, area_bits = 0;
	int bit_off, end, oslot;
	unsigned long index, i, off, bits;
	unsigned long *map;

	oslot = pcpu_chunk_slot(chunk);

	end = min_t(int, start + alloc_bits + PCPU_BITMAP_BLOCK_BITS,
		    pcpu_chunk_map_bits(chunk));
	map = chunk->alloc_map;

	/* pcpu_find_zero_area inlined */
again:
	index = find_next_zero_bit(map, end, start);
	index = __ALIGN_MASK(index, align_mask);
	off = index;
	if (index + alloc_bits > (unsigned long)end) {
		bit_off = end;
		goto check_end;
	}
	i = find_next_bit(map, index + alloc_bits, index);
	if (i < index + alloc_bits) {
		bits = i - off;
		if (bits > area_bits ||
		    (bits == area_bits && area_off &&
		     (!off || __ffs(off) > __ffs(area_off)))) {
			area_off = off;
			area_bits = bits;
		}
		start = i + 1;
		goto again;
	}
	bit_off = index;
check_end:
	if (bit_off >= end)
		return -1;

	/* Inlined pcpu_block_update_scan */
	if (area_bits) {
		int s_off = pcpu_off_to_block_off(area_off);
		int e_off = s_off + area_bits;
		int s_index, l_bit;
		struct pcpu_block_md *block;

		if (e_off <= PCPU_BITMAP_BLOCK_BITS) {
			s_index = pcpu_off_to_block_index(area_off);
			block = chunk->md_blocks + s_index;

			l_bit = find_last_bit(
				pcpu_index_alloc_map(chunk, s_index), s_off);
			s_off = (s_off == l_bit) ? 0 : l_bit + 1;

			pcpu_block_update(block, s_off, e_off);
		}
	}

	bitmap_set(chunk->alloc_map, bit_off, alloc_bits);

	set_bit(bit_off, chunk->bound_map);
	bitmap_clear(chunk->bound_map, bit_off + 1, alloc_bits - 1);
	set_bit(bit_off + alloc_bits, chunk->bound_map);

	chunk->free_bytes -= alloc_bits * PCPU_MIN_ALLOC_SIZE;

	if (bit_off == chunk_md->first_free)
		chunk_md->first_free = find_next_zero_bit(
			chunk->alloc_map, pcpu_chunk_map_bits(chunk),
			bit_off + alloc_bits);

	pcpu_block_update_hint_alloc(chunk, bit_off, alloc_bits);

	pcpu_chunk_relocate(chunk, oslot);

	return bit_off * PCPU_MIN_ALLOC_SIZE;
}

/* pcpu_free_area removed - never called */

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

static struct pcpu_chunk *__init pcpu_alloc_first_chunk(unsigned long tmp_addr,
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

		pcpu_block_update_hint_alloc(
			chunk, pcpu_chunk_map_bits(chunk) - offset_bits,
			offset_bits);
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

	chunk->alloc_map = pcpu_mem_zalloc(
		BITS_TO_LONGS(region_bits) * sizeof(chunk->alloc_map[0]), gfp);
	if (!chunk->alloc_map)
		goto alloc_map_fail;

	chunk->bound_map = pcpu_mem_zalloc(BITS_TO_LONGS(region_bits + 1) *
						   sizeof(chunk->bound_map[0]),
					   gfp);
	if (!chunk->bound_map)
		goto bound_map_fail;

	chunk->md_blocks = pcpu_mem_zalloc(
		pcpu_chunk_nr_blocks(chunk) * sizeof(chunk->md_blocks[0]), gfp);
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

/* pcpu_free_chunk inlined into pcpu_create_chunk */

static void pcpu_chunk_populated(struct pcpu_chunk *chunk, int page_start,
				 int page_end)
{
	int nr = page_end - page_start;

	bitmap_set(chunk->populated, page_start, nr);
	chunk->nr_populated += nr;

	pcpu_update_empty_pages(chunk, nr);
}

/* Removed: pcpu_chunk_depopulated - dead code since no chunk depopulation */
/* Removed: pcpu_populate_chunk - always returned 0, call sites simplified */

static struct pcpu_chunk *pcpu_create_chunk(gfp_t gfp);

#include "percpu-km.c"

/* Removed: pcpu_chunk_addr_search, pcpu_memcg_pre_alloc_hook,
 * pcpu_memcg_post_alloc_hook - dead code */
/* reserved parameter removed - only caller passes false */

static void __percpu *pcpu_alloc(size_t size, size_t align, gfp_t gfp)
{
	gfp_t pcpu_gfp;
	/* is_atomic removed - only caller is __alloc_percpu with GFP_KERNEL, always false */
	bool do_warn;
	static int warn_limit = 10;
	struct pcpu_chunk *chunk, *next;
	const char *err;
	int slot, off;
	/* cpu, ret removed - unused after pcpu simplifications */
	unsigned long flags;
	void __percpu *ptr;
	size_t bits, bit_align;

	gfp = current_gfp_context(gfp);

	pcpu_gfp = gfp & (GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN);
	do_warn = !(gfp & __GFP_NOWARN);

	if (unlikely(align < PCPU_MIN_ALLOC_SIZE))
		align = PCPU_MIN_ALLOC_SIZE;

	size = ALIGN(size, PCPU_MIN_ALLOC_SIZE);
	bits = size >> PCPU_MIN_ALLOC_SHIFT;
	bit_align = align >> PCPU_MIN_ALLOC_SHIFT;

	if (unlikely(!size || size > PCPU_MIN_UNIT_SIZE || align > PAGE_SIZE ||
		     !is_power_of_2(align))) {
		WARN(do_warn,
		     "illegal size (%zu) or align (%zu) for percpu allocation\n",
		     size, align);
		return NULL;
	}

	/* pcpu_memcg_pre_alloc_hook always returns true, post_alloc_hook is empty */
	/* is_atomic always false - always take mutex */
	if (gfp & __GFP_NOFAIL) {
		mutex_lock(&pcpu_alloc_mutex);
	} else if (mutex_lock_killable(&pcpu_alloc_mutex)) {
		return NULL;
	}

	spin_lock_irqsave(&pcpu_lock, flags);

	/* reserved block removed - only caller passes false */
restart:

	for (slot = pcpu_size_to_slot(size); slot <= pcpu_free_slot; slot++) {
		list_for_each_entry_safe(chunk, next, &pcpu_chunk_lists[slot],
					 list) {
			off = pcpu_find_block_fit(chunk, bits, bit_align);
			if (off < 0) {
				if (slot < PCPU_SLOT_FAIL_THRESHOLD)
					__pcpu_chunk_move(chunk, 0, true);
				continue;
			}

			off = pcpu_alloc_area(chunk, bits, bit_align, off);
			if (off >= 0) {
				/* pcpu_reintegrate_chunk inlined */
				if (chunk->isolated) {
					chunk->isolated = false;
					pcpu_chunk_relocate(chunk, -1);
				}
				goto area_found;
			}
		}
	}

	spin_unlock_irqrestore(&pcpu_lock, flags);

	/* is_atomic always false - removed dead branch */
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
	/* pcpu_stats_area_alloc removed - stats stub */
	spin_unlock_irqrestore(&pcpu_lock, flags);

	/* is_atomic always false - block always executed */
	{
		unsigned int page_end, rs, re;

		rs = PFN_DOWN(off);
		page_end = PFN_UP(off + size);

		for_each_clear_bitrange_from(rs, re, chunk->populated,
					     page_end) {
			WARN_ON(chunk->immutable);

			/* pcpu_populate_chunk always returns 0 - dead error path removed */
			spin_lock_irqsave(&pcpu_lock, flags);
			pcpu_chunk_populated(chunk, rs, re);
			spin_unlock_irqrestore(&pcpu_lock, flags);
		}

		mutex_unlock(&pcpu_alloc_mutex);
	}

	/* pcpu_balance_work scheduling removed - workfn was a no-op stub */

	/* for_each_possible_cpu simplified - single CPU */
	/* pcpu_chunk_addr(chunk, 0, 0) = chunk->base_addr + pcpu_unit_offsets[0] */
	memset((void *)((unsigned long)chunk->base_addr +
			pcpu_unit_offsets[0]) +
		       off,
	       0, size);

	ptr = __addr_to_pcpu_ptr(chunk->base_addr + off);
	/* pcpu_memcg_post_alloc_hook is empty */
	return ptr;

fail:
	/* fail_unlock label removed - no goto references it */

	/* is_atomic always false - only caller is __alloc_percpu with GFP_KERNEL */
	if (do_warn && warn_limit) {
		pr_warn("allocation failed, size=%zu align=%zu, %s\n", size,
			align, err);
		if (!--warn_limit)
			pr_info("limit reached, disable warning\n");
	}
	mutex_unlock(&pcpu_alloc_mutex);
	return NULL;
}

/* __alloc_percpu_gfp removed - only used by alloc_percpu_gfp macro which was unused */

void __percpu *__alloc_percpu(size_t size, size_t align)
{
	return pcpu_alloc(size, align, GFP_KERNEL);
}

/* pcpu_balance_workfn removed - was no-op stub, entire async mechanism removed */

/* free_percpu moved to percpu.h as static inline */

/* Stub: per_cpu_ptr_to_phys not used in minimal kernel */
phys_addr_t per_cpu_ptr_to_phys(void *addr)
{
	return __pa(addr);
}

struct pcpu_alloc_info *__init pcpu_alloc_alloc_info(int nr_groups,
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

#define PCPU_SETUP_BUG_ON(cond)                                        \
	do {                                                           \
		if (unlikely(cond)) {                                  \
			pr_emerg("failed to initialize, %s\n", #cond); \
			pr_emerg("cpu_possible_mask=%*pb\n",           \
				 cpumask_pr_args(cpu_possible_mask));  \
			/* pcpu_dump_alloc_info removed */             \
			BUG();                                         \
		}                                                      \
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
	PCPU_SETUP_BUG_ON(ai->nr_groups !=
			  1); /* pcpu_verify_alloc_info inlined */

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
		}
	}
	/* for_each_possible_cpu simplified - single CPU */
	PCPU_SETUP_BUG_ON(unit_map[0] == UINT_MAX);

#undef PCPU_SETUP_BUG_ON
	/* pcpu_dump_alloc_info removed */

	pcpu_group_sizes = group_sizes;
	pcpu_unit_offsets = unit_off;

	pcpu_unit_pages = ai->unit_size >> PAGE_SHIFT;
	pcpu_unit_size = pcpu_unit_pages << PAGE_SHIFT;
	pcpu_chunk_struct_size =
		struct_size(chunk, populated, BITS_TO_LONGS(pcpu_unit_pages));
	/* pcpu_stats_save_ai removed - stats stub */
	pcpu_sidelined_slot = __pcpu_size_to_slot(pcpu_unit_size) + 1;
	pcpu_free_slot = pcpu_sidelined_slot + 1;
	pcpu_to_depopulate_slot = pcpu_free_slot + 1;
	pcpu_nr_slots = pcpu_to_depopulate_slot + 1;
	pcpu_chunk_lists = memblock_alloc(
		pcpu_nr_slots * sizeof(pcpu_chunk_lists[0]), SMP_CACHE_BYTES);
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
	pcpu_chunk_relocate(pcpu_first_chunk, -1);

	/* pcpu_stats_chunk_alloc removed - stats stub */
}

void __init setup_per_cpu_areas(void)
{
	const size_t unit_size = roundup_pow_of_two(
		max_t(size_t, PCPU_MIN_UNIT_SIZE, PERCPU_DYNAMIC_RESERVE));
	struct pcpu_alloc_info *ai;
	void *fc;

	ai = pcpu_alloc_alloc_info(1, 1);
	/* memblock_alloc_from inlined */
	fc = memblock_alloc_try_nid(unit_size, PAGE_SIZE, __pa(MAX_DMA_ADDRESS),
				    MEMBLOCK_ALLOC_ACCESSIBLE, NUMA_NO_NODE);
	if (!ai || !fc)
		panic("Failed to allocate memory for percpu areas.");

	ai->dyn_size = unit_size;
	ai->unit_size = unit_size;
	ai->atom_size = unit_size;
	ai->alloc_size = unit_size;
	ai->groups[0].nr_units = 1;
	ai->groups[0].cpu_map[0] = 0;

	pcpu_setup_first_chunk(ai, fc);
	pcpu_free_alloc_info(ai);
}

/* percpu_enable_async removed - entire async mechanism removed (workfn was no-op) */
