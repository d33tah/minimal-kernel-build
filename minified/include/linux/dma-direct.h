/* Minimal dma-direct.h - most functions removed as they have no callers */
#ifndef _LINUX_DMA_DIRECT_H
#define _LINUX_DMA_DIRECT_H 1

#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/memblock.h>
#include <linux/mem_encrypt.h>

struct bus_dma_region {
	phys_addr_t cpu_start;
	dma_addr_t dma_start;
	u64 size;
	u64 offset;
};

/* phys_to_dma_unencrypted, phys_to_dma, dma_to_phys, dma_direct_* removed - no callers */

#endif
