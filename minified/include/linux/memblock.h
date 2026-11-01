/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_MEMBLOCK_H
#define _LINUX_MEMBLOCK_H

#include <linux/mm.h>

extern unsigned long max_low_pfn;

/*
 * highest page
 */
extern unsigned long max_pfn;

/**
 * enum memblock_flags - definition of memory region attributes
 * @MEMBLOCK_NONE: no special request
 * @MEMBLOCK_HOTPLUG: memory region indicated in the firmware-provided memory
 * map during early boot as hot(un)pluggable system RAM (e.g., memory range
 * that might get hotunplugged later). With "movable_node" set on the kernel
 * commandline, try keeping this memory region hotunpluggable. Does not apply
 * to memblocks added ("hotplugged") after early boot.
 * @MEMBLOCK_MIRROR: mirrored region
 * @MEMBLOCK_NOMAP: don't add to kernel direct mapping and treat as
 * reserved in the memory map; refer to memblock_mark_nomap() description
 * for further details
 * @MEMBLOCK_DRIVER_MANAGED: memory region that is always detected and added
 * via a driver, and never indicated in the firmware-provided memory map as
 * system RAM. This corresponds to IORESOURCE_SYSRAM_DRIVER_MANAGED in the
 * kernel resource tree.
 */
enum memblock_flags {
	MEMBLOCK_NONE = 0x0, /* No special request */
};

/**
 * struct memblock_region - represents a memory region
 * @base: base address of the region
 * @size: size of the region
 * @flags: memory region attributes
 * @nid: NUMA node id
 */
struct memblock_region {
	phys_addr_t base;
	phys_addr_t size;
	enum memblock_flags flags;
};

/**
 * struct memblock_type - collection of memory regions of certain type
 * @cnt: number of regions
 * @max: size of the allocated array
 * @total_size: size of all regions
 * @regions: array of regions
 * @name: the memory type symbolic name
 */
struct memblock_type {
	unsigned long cnt;
	unsigned long max;
	struct memblock_region *regions;
};

/**
 * struct memblock - memblock allocator metadata
 * @bottom_up: is bottom up direction?
 * @current_limit: physical address of the current allocation limit
 * @memory: usable memory regions
 * @reserved: reserved memory regions
 */
struct memblock {
	phys_addr_t current_limit;
	struct memblock_type memory;
	struct memblock_type reserved;
};

extern struct memblock memblock;

#define __init_memblock __meminit
#define __initdata_memblock __meminitdata
int memblock_add(phys_addr_t base, phys_addr_t size);
int memblock_phys_free(phys_addr_t base, phys_addr_t size);
int memblock_reserve(phys_addr_t base, phys_addr_t size);
void memblock_trim_memory(phys_addr_t align);

void memblock_free_all(void);

/* Low level functions */
void __next_mem_range(u64 *idx, int nid, enum memblock_flags flags,
		      struct memblock_type *type_a,
		      struct memblock_type *type_b, phys_addr_t *out_start,
		      phys_addr_t *out_end, int *out_nid);

void __next_mem_range_rev(u64 *idx, int nid, enum memblock_flags flags,
			  struct memblock_type *type_a,
			  struct memblock_type *type_b, phys_addr_t *out_start,
			  phys_addr_t *out_end, int *out_nid);

#define __for_each_mem_range(i, type_a, type_b, nid, flags, p_start, p_end,   \
			     p_nid)                                           \
	for (i = 0, __next_mem_range(&i, nid, flags, type_a, type_b, p_start, \
				     p_end, p_nid);                           \
	     i != (u64)ULLONG_MAX; __next_mem_range(                          \
		     &i, nid, flags, type_a, type_b, p_start, p_end, p_nid))

#define __for_each_mem_range_rev(i, type_a, type_b, nid, flags, p_start,  \
				 p_end, p_nid)                            \
	for (i = (u64)ULLONG_MAX,                                         \
	    __next_mem_range_rev(&i, nid, flags, type_a, type_b, p_start, \
				 p_end, p_nid);                           \
	     i != (u64)ULLONG_MAX; __next_mem_range_rev(                  \
		     &i, nid, flags, type_a, type_b, p_start, p_end, p_nid))

/**
 * for_each_reserved_mem_range - iterate over all reserved memblock areas
 * @i: u64 used as loop variable
 * @p_start: ptr to phys_addr_t for start address of the range, can be %NULL
 * @p_end: ptr to phys_addr_t for end address of the range, can be %NULL
 *
 * Walks over reserved areas of memblock. Available as soon as memblock
 * is initialized.
 */
#define for_each_reserved_mem_range(i, p_start, p_end)                  \
	__for_each_mem_range(i, &memblock.reserved, NULL, NUMA_NO_NODE, \
			     MEMBLOCK_NONE, p_start, p_end, NULL)

void __next_mem_pfn_range(int *idx, int nid, unsigned long *out_start_pfn,
			  unsigned long *out_end_pfn, int *out_nid);

/**
 * for_each_mem_pfn_range - early memory pfn range iterator
 * @i: an integer used as loop variable
 * @nid: node selector, %MAX_NUMNODES for all nodes
 * @p_start: ptr to ulong for start pfn of the range, can be %NULL
 * @p_end: ptr to ulong for end pfn of the range, can be %NULL
 * @p_nid: ptr to int for nid of the range, can be %NULL
 *
 * Walks over configured memory ranges.
 */
#define for_each_mem_pfn_range(i, nid, p_start, p_end, p_nid)              \
	for (i = -1, __next_mem_pfn_range(&i, nid, p_start, p_end, p_nid); \
	     i >= 0; __next_mem_pfn_range(&i, nid, p_start, p_end, p_nid))

/**
 * for_each_free_mem_range - iterate through free memblock areas
 * @i: u64 used as loop variable
 * @nid: node selector, %NUMA_NO_NODE for all nodes
 * @flags: pick from blocks based on memory attributes
 * @p_start: ptr to phys_addr_t for start address of the range, can be %NULL
 * @p_end: ptr to phys_addr_t for end address of the range, can be %NULL
 * @p_nid: ptr to int for nid of the range, can be %NULL
 *
 * Walks over free (memory && !reserved) areas of memblock.  Available as
 * soon as memblock is initialized.
 */
#define for_each_free_mem_range(i, nid, flags, p_start, p_end, p_nid)      \
	__for_each_mem_range(i, &memblock.memory, &memblock.reserved, nid, \
			     flags, p_start, p_end, p_nid)

#define for_each_free_mem_range_reverse(i, nid, flags, p_start, p_end, p_nid)  \
	__for_each_mem_range_rev(i, &memblock.memory, &memblock.reserved, nid, \
				 flags, p_start, p_end, p_nid)

static inline int memblock_get_region_node(const struct memblock_region *r)
{
	return 0;
}

/* Flags for memblock allocation APIs */
#define MEMBLOCK_ALLOC_ANYWHERE (~(phys_addr_t)0)
#define MEMBLOCK_ALLOC_ACCESSIBLE 0

/* We are using top down, so it is safe to use 0 here */
#define MEMBLOCK_LOW_LIMIT 0

phys_addr_t memblock_phys_alloc_range(phys_addr_t size, phys_addr_t align,
				      phys_addr_t start, phys_addr_t end);

void *memblock_alloc_try_nid_raw(phys_addr_t size, phys_addr_t align,
				 phys_addr_t min_addr, phys_addr_t max_addr,
				 int nid);
void *memblock_alloc_try_nid(phys_addr_t size, phys_addr_t align,
			     phys_addr_t min_addr, phys_addr_t max_addr,
			     int nid);

static __always_inline void *memblock_alloc(phys_addr_t size, phys_addr_t align)
{
	return memblock_alloc_try_nid(size, align, MEMBLOCK_LOW_LIMIT,
				      MEMBLOCK_ALLOC_ACCESSIBLE, NUMA_NO_NODE);
}

phys_addr_t memblock_start_of_DRAM(void);
bool memblock_is_region_memory(phys_addr_t base, phys_addr_t size);

/**
 * memblock_set_current_limit - Set the current allocation limit to allow
 *                         limiting allocations to what is currently
 *                         accessible during boot
 * @limit: New limit value (physical address)
 */
void memblock_set_current_limit(phys_addr_t limit);

/**
 * for_each_mem_region - itereate over memory regions
 * @region: loop variable
 */
#define for_each_mem_region(region)                                    \
	for (region = memblock.memory.regions;                         \
	     region < (memblock.memory.regions + memblock.memory.cnt); \
	     region++)

extern void *
alloc_large_system_hash(const char *tablename, unsigned long bucketsize,
			unsigned long numentries, int scale, int flags,
			unsigned int *_hash_shift, unsigned int *_hash_mask,
			unsigned long low_limit, unsigned long high_limit);

#define HASH_EARLY 0x00000001 /* Allocating during early boot? */
#define HASH_ZERO 0x00000004 /* Zero allocated hash table */

#endif
