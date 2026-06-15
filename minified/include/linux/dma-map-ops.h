#ifndef _LINUX_DMA_MAP_OPS_H
#define _LINUX_DMA_MAP_OPS_H

#include <linux/pgtable.h>

struct cma;

struct dma_map_ops;

#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE) || \
	defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) || \
	defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL)
extern bool dma_default_coherent;
static inline bool dev_is_dma_coherent(struct device *dev)
{
	return dev->dma_coherent;
}
#else
static inline bool dev_is_dma_coherent(struct device *dev)
{
	return true;
}
#endif

static inline void arch_teardown_dma_ops(struct device *dev)
{
}

#endif
