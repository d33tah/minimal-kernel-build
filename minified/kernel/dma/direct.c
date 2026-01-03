/* Minimal DMA direct.c - all functions removed as they have no external callers.
   The original functions (dma_direct_alloc, dma_direct_free, dma_direct_alloc_pages,
   dma_direct_free_pages, dma_direct_sync_sg_for_device/cpu, dma_direct_map_sg,
   dma_direct_unmap_sg, dma_direct_map_resource, dma_direct_get_required_mask,
   dma_direct_supported, dma_direct_max_mapping_size) were never called. */

#include <linux/memblock.h>
#include <linux/export.h>

unsigned int zone_dma_bits __ro_after_init = 24;
