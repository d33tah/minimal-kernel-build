#ifndef _LINUX_DMA_MAP_OPS_H
#define _LINUX_DMA_MAP_OPS_H

#include <linux/pgtable.h>

struct cma;

struct dma_map_ops;

static inline bool dev_is_dma_coherent(struct device *dev)
{
	return true;
}

#endif
