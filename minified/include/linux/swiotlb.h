/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SWIOTLB_H
#define __LINUX_SWIOTLB_H

#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/limits.h>
#include <linux/spinlock.h>

struct device;
struct page;
struct scatterlist;

#define SWIOTLB_VERBOSE	(1 << 0) /* verbose initialization */
#define SWIOTLB_FORCE	(1 << 1) /* force bounce buffering */
#define SWIOTLB_ANY	(1 << 2) /* allow any memory for the buffer */

/*
 * Maximum allowable number of contiguous slabs to map,
 * must be a power of 2.  What is the appropriate value ?
 * The complexity of {map,unmap}_single is linearly dependent on this value.
 */
#define IO_TLB_SEGSIZE	128

/*
 * log of the size of each IO TLB slab.  The number of slabs is command line
 * controllable.
 */
#define IO_TLB_SHIFT 11
#define IO_TLB_SIZE (1 << IO_TLB_SHIFT)

/* default to 64MB */
#define IO_TLB_DEFAULT_SIZE (64UL<<20)

unsigned long swiotlb_size_or_default(void);
void __init swiotlb_init_remap(bool addressing_limit, unsigned int flags,
	int (*remap)(void *tlb, unsigned long nslabs));
int swiotlb_init_late(size_t size, gfp_t gfp_mask,
	int (*remap)(void *tlb, unsigned long nslabs));
extern void __init swiotlb_update_mem_attributes(void);

phys_addr_t swiotlb_tbl_map_single(struct device *hwdev, phys_addr_t phys,
		size_t mapping_size, size_t alloc_size,
		unsigned int alloc_aligned_mask, enum dma_data_direction dir,
		unsigned long attrs);

extern void swiotlb_tbl_unmap_single(struct device *hwdev,
				     phys_addr_t tlb_addr,
				     size_t mapping_size,
				     enum dma_data_direction dir,
				     unsigned long attrs);

void swiotlb_sync_single_for_device(struct device *dev, phys_addr_t tlb_addr,
		size_t size, enum dma_data_direction dir);
void swiotlb_sync_single_for_cpu(struct device *dev, phys_addr_t tlb_addr,
		size_t size, enum dma_data_direction dir);
dma_addr_t swiotlb_map(struct device *dev, phys_addr_t phys,
		size_t size, enum dma_data_direction dir, unsigned long attrs);

static inline void swiotlb_init(bool addressing_limited, unsigned int flags)
{
}
static inline bool is_swiotlb_buffer(struct device *dev, phys_addr_t paddr)
{
	return false;
}
static inline bool is_swiotlb_force_bounce(struct device *dev)
{
	return false;
}
static inline void swiotlb_exit(void)
{
}
static inline unsigned int swiotlb_max_segment(void)
{
	return 0;
}
static inline size_t swiotlb_max_mapping_size(struct device *dev)
{
	return SIZE_MAX;
}

static inline bool is_swiotlb_active(struct device *dev)
{
	return false;
}

static inline void swiotlb_adjust_size(unsigned long size)
{
}

extern void swiotlb_print_info(void);

static inline struct page *swiotlb_alloc(struct device *dev, size_t size)
{
	return NULL;
}
static inline bool swiotlb_free(struct device *dev, struct page *page,
				size_t size)
{
	return false;
}
static inline bool is_swiotlb_for_alloc(struct device *dev)
{
	return false;
}

extern phys_addr_t swiotlb_unencrypted_base;

#endif /* __LINUX_SWIOTLB_H */
