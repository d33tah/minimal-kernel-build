/* Minimal swiotlb.h - SWIOTLB not used in this kernel */
#ifndef __LINUX_SWIOTLB_H
#define __LINUX_SWIOTLB_H

#include <linux/types.h>
#include <linux/limits.h>
#include <linux/dma-direction.h>

struct device;
struct page;
struct scatterlist;

#define IO_TLB_SEGSIZE	128
#define IO_TLB_SHIFT 11
#define IO_TLB_SIZE (1 << IO_TLB_SHIFT)
#define IO_TLB_DEFAULT_SIZE (64UL<<20)

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

/* Stubs needed by inline functions in kernel/dma/direct.h */
static inline void swiotlb_sync_single_for_device(struct device *dev, phys_addr_t addr,
		size_t size, enum dma_data_direction dir)
{
}
static inline void swiotlb_sync_single_for_cpu(struct device *dev, phys_addr_t addr,
		size_t size, enum dma_data_direction dir)
{
}
static inline dma_addr_t swiotlb_map(struct device *dev, phys_addr_t phys,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	return 0;
}
static inline void swiotlb_tbl_unmap_single(struct device *dev,
				     phys_addr_t tlb_addr,
				     size_t mapping_size,
				     enum dma_data_direction dir,
				     unsigned long attrs)
{
}

#endif /* __LINUX_SWIOTLB_H */
