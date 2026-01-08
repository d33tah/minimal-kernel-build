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
/* scatterlist.h removed - unused */
#include <linux/bug.h>
#include <linux/mem_encrypt.h>


/* DMA_ATTR_*, DMA_MAPPING_ERROR, DMA_BIT_MASK removed - unused */

#endif
