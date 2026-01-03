/* Minimal dma-direct.h - most functions removed as they have no callers */
#ifndef _LINUX_DMA_DIRECT_H
#define _LINUX_DMA_DIRECT_H 1

#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/memblock.h>
#include <linux/mem_encrypt.h>
#include <linux/swiotlb.h>

extern unsigned int zone_dma_bits;

struct bus_dma_region {
	phys_addr_t cpu_start;
	dma_addr_t dma_start;
	u64 size;
	u64 offset;
};

static inline dma_addr_t phys_to_dma_unencrypted(struct device *dev,
						 phys_addr_t paddr)
{
	return paddr;
}

static inline dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	return __sme_set(phys_to_dma_unencrypted(dev, paddr));
}

static inline phys_addr_t dma_to_phys(struct device *dev, dma_addr_t dma_addr)
{
	return __sme_clr(dma_addr);
}

/* All dma_direct_* function declarations removed - no callers */

#endif
