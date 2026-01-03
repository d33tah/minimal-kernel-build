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


/* All DMA mapping functions and inline helpers removed - no callers */

#endif
