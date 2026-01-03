#ifndef _LINUX_DMA_MAPPING_H
#define _LINUX_DMA_MAPPING_H

#include <linux/sizes.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/err.h>
enum dma_data_direction {
	DMA_BIDIRECTIONAL = 0,
	DMA_TO_DEVICE = 1,
	DMA_FROM_DEVICE = 2,
	DMA_NONE = 3,
};
/* end dma-direction.h */
#include <linux/scatterlist.h>
#include <linux/bug.h>
#include <linux/mem_encrypt.h>


#define DMA_ATTR_NO_KERNEL_MAPPING	(1UL << 4)
#define DMA_ATTR_SKIP_CPU_SYNC		(1UL << 5)
#define DMA_ATTR_NO_WARN	(1UL << 8)

#define DMA_MAPPING_ERROR		(~(dma_addr_t)0)

#define DMA_BIT_MASK(n)	(((n) == 64) ? ~0ULL : ((1ULL<<(n))-1))


/* dma_map_page_attrs, dma_unmap_page_attrs, dma_map_sg_attrs, dma_unmap_sg_attrs,
   dma_map_sgtable, dma_map_resource, dma_unmap_resource, dma_sync_single_for_cpu,
   dma_sync_single_for_device, dma_sync_sg_for_cpu, dma_sync_sg_for_device,
   dma_alloc_attrs, dma_free_attrs, dmam_alloc_attrs, dmam_free_coherent,
   dma_get_sgtable_attrs, dma_mmap_attrs, dma_can_mmap, dma_get_required_mask,
   dma_set_coherent_mask, dma_max_mapping_size, dma_need_sync, dma_get_merge_boundary,
   dma_alloc/free/vmap/vunmap/mmap_noncontiguous, dma_alloc/free/mmap_pages removed - unused */

int dma_supported(struct device *dev, u64 mask);
int dma_set_mask(struct device *dev, u64 mask);

static inline u64 dma_get_mask(struct device *dev)
{
	if (dev->dma_mask && *dev->dma_mask)
		return *dev->dma_mask;
	return DMA_BIT_MASK(32);
}


/* dma_addressing_limited removed - never called */

/* DEFINE_DMA_UNMAP_ADDR/LEN, dma_unmap_addr/len/set removed - unused */

#endif  
