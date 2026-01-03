#include <linux/memblock.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/dma-map-ops.h>
#include <linux/scatterlist.h>
#include <linux/pfn.h>
#include <linux/vmalloc.h>
#include <asm/set_memory.h>
#include <linux/slab.h>
#include "direct.h"

unsigned int zone_dma_bits __ro_after_init = 24;

/* force_dma_unencrypted always returns false */
static inline dma_addr_t phys_to_dma_direct(struct device *dev,
					    phys_addr_t phys)
{
	return phys_to_dma(dev, phys);
}

static inline struct page *dma_direct_to_page(struct device *dev,
					      dma_addr_t dma_addr)
{
	return pfn_to_page(PHYS_PFN(dma_to_phys(dev, dma_addr)));
}

u64 dma_direct_get_required_mask(struct device *dev)
{
	phys_addr_t phys = (phys_addr_t)(max_pfn - 1) << PAGE_SHIFT;
	u64 max_dma = phys_to_dma_direct(dev, phys);

	return (1ULL << (fls64(max_dma) - 1)) * 2 - 1;
}

static gfp_t dma_direct_optimal_gfp_mask(struct device *dev, u64 dma_mask,
					 u64 *phys_limit)
{
	u64 dma_limit = min_not_zero(dma_mask, dev->bus_dma_limit);

	*phys_limit = dma_to_phys(dev, dma_limit);
	if (*phys_limit <= DMA_BIT_MASK(zone_dma_bits))
		return GFP_DMA;
	if (*phys_limit <= DMA_BIT_MASK(32))
		return GFP_DMA32;
	return 0;
}

static bool dma_coherent_ok(struct device *dev, phys_addr_t phys, size_t size)
{
	dma_addr_t dma_addr = phys_to_dma_direct(dev, phys);

	if (dma_addr == DMA_MAPPING_ERROR)
		return false;
	return dma_addr + size - 1 <=
	       min_not_zero(dev->coherent_dma_mask, dev->bus_dma_limit);
}

/* Removed: dma_set_decrypted, dma_set_encrypted - always return 0 (~8 LOC) */

static void __dma_direct_free_pages(struct device *dev, struct page *page,
				    size_t size)
{
	dma_free_contiguous(dev, page, size);
}

static struct page *__dma_direct_alloc_pages(struct device *dev, size_t size,
					     gfp_t gfp, bool allow_highmem)
{
	int node = dev_to_node(dev);
	struct page *page = NULL;
	u64 phys_limit;

	WARN_ON_ONCE(!PAGE_ALIGNED(size));

	gfp |= dma_direct_optimal_gfp_mask(dev, dev->coherent_dma_mask,
					   &phys_limit);
	page = dma_alloc_contiguous(dev, size, gfp);
	/* PageHighMem always returns false */
	if (page && !dma_coherent_ok(dev, page_to_phys(page), size)) {
		dma_free_contiguous(dev, page, size);
		page = NULL;
	}
	if (!page)
		page = alloc_pages_node(node, gfp, get_order(size));
	if (page && !dma_coherent_ok(dev, page_to_phys(page), size)) {
		dma_free_contiguous(dev, page, size);
		page = NULL;
	}

	return page;
}

/* Removed: dma_direct_use_pool, dma_direct_alloc_from_pool - never called (~10 LOC) */

static void *dma_direct_alloc_no_mapping(struct device *dev, size_t size,
					 dma_addr_t *dma_handle, gfp_t gfp)
{
	struct page *page;

	/* arch_dma_prep_coherent is an empty stub */
	page = __dma_direct_alloc_pages(dev, size, gfp & ~__GFP_ZERO, true);
	if (!page)
		return NULL;
	*dma_handle = phys_to_dma_direct(dev, page_to_phys(page));
	return page;
}

void *dma_direct_alloc(struct device *dev, size_t size, dma_addr_t *dma_handle,
		       gfp_t gfp, unsigned long attrs)
{
	struct page *page;
	void *ret;

	size = PAGE_ALIGN(size);
	if (attrs & DMA_ATTR_NO_WARN)
		gfp |= __GFP_NOWARN;

	if (attrs & DMA_ATTR_NO_KERNEL_MAPPING)
		return dma_direct_alloc_no_mapping(dev, size, dma_handle, gfp);

	page = __dma_direct_alloc_pages(dev, size, gfp & ~__GFP_ZERO, true);
	if (!page)
		return NULL;

	/* PageHighMem always returns false */
	ret = page_address(page);
	memset(ret, 0, size);

	*dma_handle = phys_to_dma_direct(dev, page_to_phys(page));
	return ret;
}

/* force_dma_unencrypted always returns false */
void dma_direct_free(struct device *dev, size_t size, void *cpu_addr,
		     dma_addr_t dma_addr, unsigned long attrs)
{
	if (attrs & DMA_ATTR_NO_KERNEL_MAPPING) {
		dma_free_contiguous(dev, cpu_addr, size);
		return;
	}
	if (is_vmalloc_addr(cpu_addr))
		vunmap(cpu_addr);
	/* dma_set_encrypted now always returns 0, no need to call */
	__dma_direct_free_pages(dev, dma_direct_to_page(dev, dma_addr), size);
}

/* force_dma_unencrypted always returns false */
struct page *dma_direct_alloc_pages(struct device *dev, size_t size,
				    dma_addr_t *dma_handle,
				    enum dma_data_direction dir, gfp_t gfp)
{
	struct page *page;

	page = __dma_direct_alloc_pages(dev, size, gfp, false);
	if (!page)
		return NULL;
	/* dma_set_decrypted now always returns 0 */
	memset(page_address(page), 0, size);
	*dma_handle = phys_to_dma_direct(dev, page_to_phys(page));
	return page;
}

void dma_direct_free_pages(struct device *dev, size_t size, struct page *page,
			   dma_addr_t dma_addr, enum dma_data_direction dir)
{
	__dma_direct_free_pages(dev, page, size);
}

#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE) || defined(CONFIG_SWIOTLB)
/* dev_is_dma_coherent always returns true, so this function is empty */
void dma_direct_sync_sg_for_device(struct device *dev, struct scatterlist *sgl,
				   int nents, enum dma_data_direction dir)
{
}
#endif

#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) ||         \
	defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL) || \
	defined(CONFIG_SWIOTLB)
/* dev_is_dma_coherent always returns true, arch_dma_mark_clean is empty stub */
void dma_direct_sync_sg_for_cpu(struct device *dev, struct scatterlist *sgl,
				int nents, enum dma_data_direction dir)
{
}

void dma_direct_unmap_sg(struct device *dev, struct scatterlist *sgl, int nents,
			 enum dma_data_direction dir, unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i)
		dma_direct_unmap_page(dev, sg->dma_address, sg_dma_len(sg), dir,
				      attrs);
}
#endif

int dma_direct_map_sg(struct device *dev, struct scatterlist *sgl, int nents,
		      enum dma_data_direction dir, unsigned long attrs)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sgl, sg, nents, i) {
		sg->dma_address = dma_direct_map_page(
			dev, sg_page(sg), sg->offset, sg->length, dir, attrs);
		if (sg->dma_address == DMA_MAPPING_ERROR)
			goto out_unmap;
		sg_dma_len(sg) = sg->length;
	}

	return nents;

out_unmap:
	dma_direct_unmap_sg(dev, sgl, i, dir, attrs | DMA_ATTR_SKIP_CPU_SYNC);
	return -EIO;
}

dma_addr_t dma_direct_map_resource(struct device *dev, phys_addr_t paddr,
				   size_t size, enum dma_data_direction dir,
				   unsigned long attrs)
{
	dma_addr_t dma_addr = paddr;

	if (unlikely(!dma_capable(dev, dma_addr, size, false))) {
		dev_err_once(
			dev,
			"DMA addr %pad+%zu overflow (mask %llx, bus limit %llx).\n",
			&dma_addr, size, *dev->dma_mask, dev->bus_dma_limit);
		WARN_ON_ONCE(1);
		return DMA_MAPPING_ERROR;
	}

	return dma_addr;
}

/* dma_direct_supported, dma_direct_max_mapping_size removed - no callers */
