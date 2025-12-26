/* Stub: DMA not needed for Hello World kernel */
#include <linux/dma-map-ops.h>
#include <linux/export.h>

bool dma_default_coherent;

dma_addr_t dma_map_page_attrs(struct device *dev, struct page *page,
			      size_t offset, size_t size,
			      enum dma_data_direction dir, unsigned long attrs)
{
	return DMA_MAPPING_ERROR;
}

void dma_unmap_page_attrs(struct device *dev, dma_addr_t addr, size_t size,
			  enum dma_data_direction dir, unsigned long attrs)
{
}

unsigned int dma_map_sg_attrs(struct device *dev, struct scatterlist *sg,
			      int nents, enum dma_data_direction dir,
			      unsigned long attrs)
{
	return 0;
}

int dma_map_sgtable(struct device *dev, struct sg_table *sgt,
		    enum dma_data_direction dir, unsigned long attrs)
{
	return -ENOMEM;
}

void dma_unmap_sg_attrs(struct device *dev, struct scatterlist *sg, int nents,
			enum dma_data_direction dir, unsigned long attrs)
{
}

void dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr, size_t size,
			     enum dma_data_direction dir)
{
}

void dma_sync_single_for_device(struct device *dev, dma_addr_t addr,
				size_t size, enum dma_data_direction dir)
{
}

void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, int nelems,
			 enum dma_data_direction dir)
{
}

void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
			    int nelems, enum dma_data_direction dir)
{
}

pgprot_t dma_pgprot(struct device *dev, pgprot_t prot, unsigned long attrs)
{
	return prot;
}

u64 dma_get_required_mask(struct device *dev)
{
	return DMA_BIT_MASK(32);
}

void *dma_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle,
		      gfp_t flag, unsigned long attrs)
{
	return NULL;
}

void dma_free_attrs(struct device *dev, size_t size, void *cpu_addr,
		    dma_addr_t dma_handle, unsigned long attrs)
{
}

int dma_supported(struct device *dev, u64 mask)
{
	return 1;
}

int dma_set_mask(struct device *dev, u64 mask)
{
	return 0;
}

int dma_set_coherent_mask(struct device *dev, u64 mask)
{
	return 0;
}
