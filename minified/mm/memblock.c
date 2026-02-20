
#include <linux/memblock.h>

#include <linux/io.h>

#include "internal.h"

#define INIT_MEMBLOCK_REGIONS 16 /* Reduced from 128 for minimal boot */

#ifndef INIT_MEMBLOCK_RESERVED_REGIONS
#define INIT_MEMBLOCK_RESERVED_REGIONS INIT_MEMBLOCK_REGIONS
#endif

struct pglist_data __refdata contig_page_data;

unsigned long max_low_pfn;
unsigned long min_low_pfn;
unsigned long max_pfn;

static struct memblock_region
	memblock_memory_init_regions[INIT_MEMBLOCK_REGIONS] __initdata_memblock;
static struct memblock_region memblock_reserved_init_regions
	[INIT_MEMBLOCK_RESERVED_REGIONS] __initdata_memblock;

struct memblock memblock __initdata_memblock = {
	.memory.regions = memblock_memory_init_regions,
	.memory.cnt = 1,
	.memory.max = INIT_MEMBLOCK_REGIONS,
	.memory.name = "memory",

	.reserved.regions = memblock_reserved_init_regions,
	.reserved.cnt = 1,
	.reserved.max = INIT_MEMBLOCK_RESERVED_REGIONS,
	.reserved.name = "reserved",

	.current_limit = MEMBLOCK_ALLOC_ANYWHERE,
};

static __refdata struct memblock_type *memblock_memory = &memblock.memory;

#define for_each_memblock_type(i, memblock_type, rgn)                         \
	for (i = 0, rgn = &memblock_type->regions[0]; i < memblock_type->cnt; \
	     i++, rgn = &memblock_type->regions[i])

static inline phys_addr_t memblock_cap_size(phys_addr_t base, phys_addr_t *size)
{
	return *size = min(*size, PHYS_ADDR_MAX - base);
}

static phys_addr_t __init_memblock memblock_find_in_range_node(
	phys_addr_t size, phys_addr_t align, phys_addr_t start, phys_addr_t end,
	int nid, enum memblock_flags flags)
{
	phys_addr_t this_start, this_end, cand;
	u64 i;

	if (end == MEMBLOCK_ALLOC_ACCESSIBLE ||
	    end == MEMBLOCK_ALLOC_NOLEAKTRACE)
		end = memblock.current_limit;

	start = max_t(phys_addr_t, start, PAGE_SIZE);
	end = max(start, end);

	for_each_free_mem_range_reverse(i, nid, flags, &this_start, &this_end,
					NULL) {
		this_start = clamp(this_start, start, end);
		this_end = clamp(this_end, start, end);

		if (this_end < size)
			continue;

		cand = round_down(this_end - size, align);
		if (cand >= this_start)
			return cand;
	}

	return 0;
}

static void __init_memblock memblock_remove_region(struct memblock_type *type,
						   unsigned long r)
{
	type->total_size -= type->regions[r].size;
	memmove(&type->regions[r], &type->regions[r + 1],
		(type->cnt - (r + 1)) * sizeof(type->regions[r]));
	type->cnt--;

	if (type->cnt == 0) {
		WARN_ON(type->total_size != 0);
		type->cnt = 1;
		type->regions[0].base = 0;
		type->regions[0].size = 0;
		type->regions[0].flags = 0;
	}
}

void __init memblock_discard(void)
{
	/* Arrays are never resized, so always equal to init_regions - nothing
	 * to free */
	memblock_memory = NULL;
}

static int __init_memblock memblock_double_array(struct memblock_type *type,
						 phys_addr_t new_area_start,
						 phys_addr_t new_area_size)
{
	/* Minimal kernel never exceeds INIT_MEMBLOCK_REGIONS (16) */
	panic("memblock: region array overflow, type=%s cnt=%lu max=%lu\n",
	      type->name, (unsigned long)type->cnt, (unsigned long)type->max);
}

static void __init_memblock memblock_insert_region(struct memblock_type *type,
						   int idx, phys_addr_t base,
						   phys_addr_t size, int nid,
						   enum memblock_flags flags)
{
	struct memblock_region *rgn = &type->regions[idx];

	BUG_ON(type->cnt >= type->max);
	memmove(rgn + 1, rgn, (type->cnt - idx) * sizeof(*rgn));
	rgn->base = base;
	rgn->size = size;
	rgn->flags = flags;
	type->cnt++;
	type->total_size += size;
}

static int __init_memblock memblock_add_range(struct memblock_type *type,
					      phys_addr_t base,
					      phys_addr_t size, int nid,
					      enum memblock_flags flags)
{
	bool insert = false;
	phys_addr_t obase = base;
	phys_addr_t end = base + memblock_cap_size(base, &size);
	int idx, nr_new;
	struct memblock_region *rgn;

	if (!size)
		return 0;

	if (type->regions[0].size == 0) {
		WARN_ON(type->cnt != 1 || type->total_size);
		type->regions[0].base = base;
		type->regions[0].size = size;
		type->regions[0].flags = flags;
		type->total_size = size;
		return 0;
	}
repeat:

	base = obase;
	nr_new = 0;

	for_each_memblock_type(idx, type, rgn)
	{
		phys_addr_t rbase = rgn->base;
		phys_addr_t rend = rbase + rgn->size;

		if (rbase >= end)
			break;
		if (rend <= base)
			continue;

		if (rbase > base) {
			WARN_ON(flags != rgn->flags);
			nr_new++;
			if (insert)
				memblock_insert_region(type, idx++, base,
						       rbase - base, nid,
						       flags);
		}

		base = min(rend, end);
	}

	if (base < end) {
		nr_new++;
		if (insert)
			memblock_insert_region(type, idx, base, end - base, nid,
					       flags);
	}

	if (!nr_new)
		return 0;

	if (!insert) {
		while (type->cnt + nr_new > type->max)
			if (memblock_double_array(type, obase, size) < 0)
				return -ENOMEM;
		insert = true;
		goto repeat;
	} else {
		int i = 0;
		while (i < type->cnt - 1) {
			struct memblock_region *this = &type->regions[i];
			struct memblock_region *next = &type->regions[i + 1];
			if (this->base + this->size != next->base ||
			    this->flags != next->flags) {
				BUG_ON(this->base + this->size > next->base);
				i++;
				continue;
			}
			this->size += next->size;
			memmove(next, next + 1,
				(type->cnt - (i + 2)) * sizeof(*next));
			type->cnt--;
		}
		return 0;
	}
}

int __init_memblock memblock_add(phys_addr_t base, phys_addr_t size)
{
	return memblock_add_range(&memblock.memory, base, size, MAX_NUMNODES,
				  0);
}

void __init_memblock memblock_free(void *ptr, size_t size)
{
	if (ptr)
		memblock_phys_free(__pa(ptr), size);
}

int __init_memblock memblock_phys_free(phys_addr_t base, phys_addr_t size)
{
	struct memblock_type *type = &memblock.reserved;
	phys_addr_t end = base + memblock_cap_size(base, &size);
	int idx, start_rgn = 0, end_rgn = 0, i;
	struct memblock_region *rgn;

	if (!size)
		return 0;

	while (type->cnt + 2 > type->max)
		if (memblock_double_array(type, base, size) < 0)
			return -ENOMEM;

	for_each_memblock_type(idx, type, rgn)
	{
		phys_addr_t rbase = rgn->base;
		phys_addr_t rend = rbase + rgn->size;

		if (rbase >= end)
			break;
		if (rend <= base)
			continue;

		if (rbase < base) {
			rgn->base = base;
			rgn->size -= base - rbase;
			type->total_size -= base - rbase;
			memblock_insert_region(type, idx, rbase, base - rbase,
					       0, rgn->flags);
		} else if (rend > end) {
			rgn->base = end;
			rgn->size -= end - rbase;
			type->total_size -= end - rbase;
			memblock_insert_region(type, idx--, rbase, end - rbase,
					       0, rgn->flags);
		} else {
			if (!end_rgn)
				start_rgn = idx;
			end_rgn = idx + 1;
		}
	}

	for (i = end_rgn - 1; i >= start_rgn; i--)
		memblock_remove_region(type, i);
	return 0;
}

int __init_memblock memblock_reserve(phys_addr_t base, phys_addr_t size)
{
	return memblock_add_range(&memblock.reserved, base, size, MAX_NUMNODES,
				  0);
}

void __next_mem_range(u64 *idx, int nid, enum memblock_flags flags,
		      struct memblock_type *type_a,
		      struct memblock_type *type_b, phys_addr_t *out_start,
		      phys_addr_t *out_end, int *out_nid)
{
	int idx_a = *idx & 0xffffffff;
	int idx_b = *idx >> 32;

	if (WARN_ONCE(
		    nid == MAX_NUMNODES,
		    "Usage of MAX_NUMNODES is deprecated. Use NUMA_NO_NODE instead\n"))
		nid = NUMA_NO_NODE;

	for (; idx_a < type_a->cnt; idx_a++) {
		struct memblock_region *m = &type_a->regions[idx_a];

		phys_addr_t m_start = m->base;
		phys_addr_t m_end = m->base + m->size;

		if (!type_b) {
			if (out_start)
				*out_start = m_start;
			if (out_end)
				*out_end = m_end;
			if (out_nid)
				*out_nid = 0;
			idx_a++;
			*idx = (u32)idx_a | (u64)idx_b << 32;
			return;
		}

		for (; idx_b < type_b->cnt + 1; idx_b++) {
			struct memblock_region *r;
			phys_addr_t r_start;
			phys_addr_t r_end;

			r = &type_b->regions[idx_b];
			r_start = idx_b ? r[-1].base + r[-1].size : 0;
			r_end = idx_b < type_b->cnt ? r->base : PHYS_ADDR_MAX;

			if (r_start >= m_end)
				break;

			if (m_start < r_end) {
				if (out_start)
					*out_start = max(m_start, r_start);
				if (out_end)
					*out_end = min(m_end, r_end);
				if (out_nid)
					*out_nid = 0;

				if (m_end <= r_end)
					idx_a++;
				else
					idx_b++;
				*idx = (u32)idx_a | (u64)idx_b << 32;
				return;
			}
		}
	}

	*idx = ULLONG_MAX;
}

void __init_memblock __next_mem_range_rev(u64 *idx, int nid,
					  enum memblock_flags flags,
					  struct memblock_type *type_a,
					  struct memblock_type *type_b,
					  phys_addr_t *out_start,
					  phys_addr_t *out_end, int *out_nid)
{
	int idx_a = *idx & 0xffffffff;
	int idx_b = *idx >> 32;

	if (WARN_ONCE(
		    nid == MAX_NUMNODES,
		    "Usage of MAX_NUMNODES is deprecated. Use NUMA_NO_NODE instead\n"))
		nid = NUMA_NO_NODE;

	if (*idx == (u64)ULLONG_MAX) {
		idx_a = type_a->cnt - 1;
		if (type_b != NULL)
			idx_b = type_b->cnt;
		else
			idx_b = 0;
	}

	for (; idx_a >= 0; idx_a--) {
		struct memblock_region *m = &type_a->regions[idx_a];

		phys_addr_t m_start = m->base;
		phys_addr_t m_end = m->base + m->size;

		if (!type_b) {
			if (out_start)
				*out_start = m_start;
			if (out_end)
				*out_end = m_end;
			if (out_nid)
				*out_nid = 0;
			idx_a--;
			*idx = (u32)idx_a | (u64)idx_b << 32;
			return;
		}

		for (; idx_b >= 0; idx_b--) {
			struct memblock_region *r;
			phys_addr_t r_start;
			phys_addr_t r_end;

			r = &type_b->regions[idx_b];
			r_start = idx_b ? r[-1].base + r[-1].size : 0;
			r_end = idx_b < type_b->cnt ? r->base : PHYS_ADDR_MAX;

			if (r_end <= m_start)
				break;

			if (m_end > r_start) {
				if (out_start)
					*out_start = max(m_start, r_start);
				if (out_end)
					*out_end = min(m_end, r_end);
				if (out_nid)
					*out_nid = 0;
				if (m_start >= r_start)
					idx_a--;
				else
					idx_b--;
				*idx = (u32)idx_a | (u64)idx_b << 32;
				return;
			}
		}
	}

	*idx = ULLONG_MAX;
}

void __init_memblock __next_mem_pfn_range(int *idx, int nid,
					  unsigned long *out_start_pfn,
					  unsigned long *out_end_pfn,
					  int *out_nid)
{
	struct memblock_type *type = &memblock.memory;
	struct memblock_region *r;

	while (++*idx < type->cnt) {
		r = &type->regions[*idx];

		if (PFN_UP(r->base) >= PFN_DOWN(r->base + r->size))
			continue;
		if (nid == MAX_NUMNODES || nid == 0)
			break;
	}
	if (*idx >= type->cnt) {
		*idx = -1;
		return;
	}

	if (out_start_pfn)
		*out_start_pfn = PFN_UP(r->base);
	if (out_end_pfn)
		*out_end_pfn = PFN_DOWN(r->base + r->size);
	if (out_nid)
		*out_nid = 0;
}

static phys_addr_t __init memblock_alloc_range_nid(phys_addr_t size,
						   phys_addr_t align,
						   phys_addr_t start,
						   phys_addr_t end, int nid,
						   bool exact_nid)
{
	phys_addr_t found;

	if (WARN_ONCE(
		    nid == MAX_NUMNODES,
		    "Usage of MAX_NUMNODES is deprecated. Use NUMA_NO_NODE instead\n"))
		nid = NUMA_NO_NODE;

	if (!align)
		align = SMP_CACHE_BYTES;

	found = memblock_find_in_range_node(size, align, start, end, nid,
					    MEMBLOCK_NONE);
	if (found && !memblock_reserve(found, size))
		return found;

	if (nid != NUMA_NO_NODE && !exact_nid) {
		found = memblock_find_in_range_node(
			size, align, start, end, NUMA_NO_NODE, MEMBLOCK_NONE);
		if (found && !memblock_reserve(found, size))
			return found;
	}

	return 0;
}

phys_addr_t __init memblock_phys_alloc_range(phys_addr_t size,
					     phys_addr_t align,
					     phys_addr_t start, phys_addr_t end)
{
	return memblock_alloc_range_nid(size, align, start, end, NUMA_NO_NODE,
					false);
}

static void *__init memblock_alloc_internal(phys_addr_t size, phys_addr_t align,
					    phys_addr_t min_addr,
					    phys_addr_t max_addr, int nid,
					    bool exact_nid)
{
	phys_addr_t alloc;

	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc_node(size, GFP_NOWAIT, nid);

	if (max_addr > memblock.current_limit)
		max_addr = memblock.current_limit;

	alloc = memblock_alloc_range_nid(size, align, min_addr, max_addr, nid,
					 exact_nid);

	if (!alloc && min_addr)
		alloc = memblock_alloc_range_nid(size, align, 0, max_addr, nid,
						 exact_nid);

	if (!alloc)
		return NULL;

	return phys_to_virt(alloc);
}

void *__init memblock_alloc_try_nid_raw(phys_addr_t size, phys_addr_t align,
					phys_addr_t min_addr,
					phys_addr_t max_addr, int nid)
{
	return memblock_alloc_internal(size, align, min_addr, max_addr, nid,
				       false);
}

void *__init memblock_alloc_try_nid(phys_addr_t size, phys_addr_t align,
				    phys_addr_t min_addr, phys_addr_t max_addr,
				    int nid)
{
	void *ptr;

	ptr = memblock_alloc_internal(size, align, min_addr, max_addr, nid,
				      false);
	if (ptr)
		memset(ptr, 0, size);

	return ptr;
}

phys_addr_t __init_memblock memblock_start_of_DRAM(void)
{
	return memblock.memory.regions[0].base;
}

bool __init_memblock memblock_is_region_memory(phys_addr_t base,
					       phys_addr_t size)
{
	struct memblock_type *type = &memblock.memory;
	unsigned int left = 0, right = type->cnt;
	phys_addr_t end = base + memblock_cap_size(base, &size);
	int idx = -1;

	do {
		unsigned int mid = (right + left) / 2;
		if (base < type->regions[mid].base)
			right = mid;
		else if (base >=
			 (type->regions[mid].base + type->regions[mid].size))
			left = mid + 1;
		else {
			idx = mid;
			break;
		}
	} while (left < right);

	if (idx == -1)
		return false;
	return (type->regions[idx].base + type->regions[idx].size) >= end;
}

void __init_memblock memblock_trim_memory(phys_addr_t align)
{
	phys_addr_t start, end, orig_start, orig_end;
	struct memblock_region *r;

	for_each_mem_region(r) {
		orig_start = r->base;
		orig_end = r->base + r->size;
		start = round_up(orig_start, align);
		end = round_down(orig_end, align);

		if (start == orig_start && end == orig_end)
			continue;

		if (start < end) {
			r->base = start;
			r->size = end - start;
		} else {
			memblock_remove_region(&memblock.memory,
					       r - memblock.memory.regions);
			r--;
		}
	}
}

void __init_memblock memblock_set_current_limit(phys_addr_t limit)
{
	memblock.current_limit = limit;
}

void __init memblock_allow_resize(void)
{
	/* no-op: array resizing replaced with panic */
}

void __init memblock_free_all(void)
{
	struct memblock_region *region;
	unsigned long count = 0;
	phys_addr_t start, end;
	u64 i;

	for_each_reserved_mem_range(i, &start, &end)
		reserve_bootmem_region(start, end);

	for_each_free_mem_range(i, NUMA_NO_NODE, MEMBLOCK_NONE, &start, &end,
				NULL) {
		unsigned long start_pfn = PFN_UP(start);
		unsigned long end_pfn =
			min_t(unsigned long, PFN_DOWN(end), max_low_pfn);
		int order;

		if (start_pfn < end_pfn) {
			while (start_pfn < end_pfn) {
				order = min(MAX_ORDER - 1UL, __ffs(start_pfn));
				while (start_pfn + (1UL << order) > end_pfn)
					order--;
				memblock_free_pages(pfn_to_page(start_pfn),
						    start_pfn, order);
				start_pfn += (1UL << order);
			}
			count += end_pfn - PFN_UP(start);
		}
	}

	atomic_long_add(count,
			&_totalram_pages); /* totalram_pages_add inlined */
}
