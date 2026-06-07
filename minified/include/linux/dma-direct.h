#ifndef _LINUX_DMA_DIRECT_H
#define _LINUX_DMA_DIRECT_H 1

#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/memblock.h>  
#include <linux/mem_encrypt.h>
#include <linux/swiotlb.h>

extern unsigned int zone_dma_bits;

struct bus_dma_region {
	phys_addr_t	cpu_start;
	dma_addr_t	dma_start;
	u64		size;
	u64		offset;
};

void *dma_direct_alloc(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs);
void dma_direct_free(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_addr, unsigned long attrs);
struct page *dma_direct_alloc_pages(struct device *dev, size_t size,
		dma_addr_t *dma_handle, enum dma_data_direction dir, gfp_t gfp);
void dma_direct_free_pages(struct device *dev, size_t size,
		struct page *page, dma_addr_t dma_addr,
		enum dma_data_direction dir);
dma_addr_t dma_direct_map_resource(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir, unsigned long attrs);

#endif  
