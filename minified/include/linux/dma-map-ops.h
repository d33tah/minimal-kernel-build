#ifndef _LINUX_DMA_MAP_OPS_H
#define _LINUX_DMA_MAP_OPS_H

#include <linux/dma-mapping.h>
#include <linux/pgtable.h>

/* struct cma, struct dma_map_ops removed - unused */
/* dma_contiguous_reserve, dma_alloc_contiguous, dma_free_contiguous removed - no callers */
/* dma_alloc_from_dev_coherent, dma_release_from_dev_coherent, dma_mmap_from_dev_coherent removed - unused */

/* All DMA pool/coherent functions removed - never called in this minimal kernel */

#ifndef pgprot_dmacoherent
#define pgprot_dmacoherent(prot)	pgprot_noncached(prot)
#endif

#endif  
