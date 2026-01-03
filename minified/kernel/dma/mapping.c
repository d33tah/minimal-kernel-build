/* Stub: DMA not needed for Hello World kernel */
#include <linux/dma-map-ops.h>
#include <linux/export.h>

bool dma_default_coherent;

/* dma_map_page_attrs, dma_unmap_page_attrs, dma_map_sg_attrs, dma_map_sgtable,
   dma_unmap_sg_attrs, dma_sync_single_for_cpu, dma_sync_single_for_device,
   dma_sync_sg_for_cpu, dma_sync_sg_for_device, dma_pgprot, dma_get_required_mask,
   dma_alloc_attrs, dma_free_attrs removed - no callers */

int dma_supported(struct device *dev, u64 mask)
{
	return 1;
}

int dma_set_mask(struct device *dev, u64 mask)
{
	return 0;
}

/* dma_set_coherent_mask removed - never called */
