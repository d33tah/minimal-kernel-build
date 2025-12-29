 
 
#ifndef _KERNEL_DMA_DIRECT_H
#define _KERNEL_DMA_DIRECT_H

#include <linux/dma-direct.h>

/* dma_direct_get_sgtable, dma_direct_can_mmap, dma_direct_mmap,
 * dma_direct_need_sync declarations removed - never defined or called */
int dma_direct_map_sg(struct device *dev, struct scatterlist *sgl, int nents,
		enum dma_data_direction dir, unsigned long attrs);
size_t dma_direct_max_mapping_size(struct device *dev);

#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE) || \
    defined(CONFIG_SWIOTLB)
void dma_direct_sync_sg_for_device(struct device *dev, struct scatterlist *sgl,
		int nents, enum dma_data_direction dir);
#else
static inline void dma_direct_sync_sg_for_device(struct device *dev,
		struct scatterlist *sgl, int nents, enum dma_data_direction dir)
{
}
#endif

#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) || \
    defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL) || \
    defined(CONFIG_SWIOTLB)
void dma_direct_unmap_sg(struct device *dev, struct scatterlist *sgl,
		int nents, enum dma_data_direction dir, unsigned long attrs);
void dma_direct_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sgl, int nents, enum dma_data_direction dir);
#else
static inline void dma_direct_unmap_sg(struct device *dev,
		struct scatterlist *sgl, int nents, enum dma_data_direction dir,
		unsigned long attrs)
{
}
static inline void dma_direct_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sgl, int nents, enum dma_data_direction dir)
{
}
#endif

/* dev_is_dma_coherent always returns true */
static inline void dma_direct_sync_single_for_cpu(struct device *dev,
		dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
	if (dir == DMA_FROM_DEVICE)
		arch_dma_mark_clean(dma_to_phys(dev, addr), size);
}

/* is_swiotlb stubs return false, dev_is_dma_coherent always returns true */
static inline dma_addr_t dma_direct_map_page(struct device *dev,
		struct page *page, unsigned long offset, size_t size,
		enum dma_data_direction dir, unsigned long attrs)
{
	dma_addr_t dma_addr = phys_to_dma(dev, page_to_phys(page) + offset);

	if (unlikely(!dma_capable(dev, dma_addr, size, true))) {
		dev_WARN_ONCE(dev, 1,
			     "DMA addr %pad+%zu overflow (mask %llx, bus limit %llx).\n",
			     &dma_addr, size, *dev->dma_mask, dev->bus_dma_limit);
		return DMA_MAPPING_ERROR;
	}
	return dma_addr;
}

/* is_swiotlb_buffer always returns false */
static inline void dma_direct_unmap_page(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		dma_direct_sync_single_for_cpu(dev, addr, size, dir);
}
#endif  
