#ifndef _LINUX_DMA_MAP_OPS_H
#define _LINUX_DMA_MAP_OPS_H

#include <linux/dma-mapping.h>
#include <linux/pgtable.h>

/* struct cma, struct dma_map_ops removed - unused */
/* dma_contiguous_reserve, dma_alloc_contiguous, dma_free_contiguous removed - no callers */
/* dma_alloc_from_dev_coherent, dma_release_from_dev_coherent, dma_mmap_from_dev_coherent removed - unused */

/* dma_common_contiguous_remap, dma_common_pages_remap, dma_common_free_remap,
   dma_common_find_pages removed - never called */

struct page *dma_alloc_from_pool(struct device *dev, size_t size,
		void **cpu_addr, gfp_t flags,
		bool (*phys_addr_ok)(struct device *, phys_addr_t, size_t));

/* dma_free_from_pool, dev_is_dma_coherent removed - no callers */

void *arch_dma_alloc(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs);
void arch_dma_free(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_addr, unsigned long attrs);

#ifndef pgprot_dmacoherent
#define pgprot_dmacoherent(prot)	pgprot_noncached(prot)
#endif

/* dma_pgprot, arch_sync_dma_for_device, arch_sync_dma_for_cpu, arch_sync_dma_for_cpu_all,
   arch_dma_prep_coherent, arch_dma_mark_clean removed - never called */

void *arch_dma_set_uncached(void *addr, size_t size);

/* arch_dma_*_direct macros removed - unused */
/* arch_teardown_dma_ops removed - inlined at single call site (empty function) */

#endif  
