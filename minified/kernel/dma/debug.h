/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2008 Advanced Micro Devices, Inc.
 *
 * Author: Joerg Roedel <joerg.roedel@amd.com>
 */

#ifndef _KERNEL_DMA_DEBUG_H
#define _KERNEL_DMA_DEBUG_H

static inline void debug_dma_map_page(struct device *dev, struct page *page,
				      size_t offset, size_t size,
				      int direction, dma_addr_t dma_addr,
				      unsigned long attrs)
{
}

static inline void debug_dma_unmap_page(struct device *dev, dma_addr_t addr,
					size_t size, int direction)
{
}

static inline void debug_dma_map_sg(struct device *dev, struct scatterlist *sg,
				    int nents, int mapped_ents, int direction,
				    unsigned long attrs)
{
}

static inline void debug_dma_unmap_sg(struct device *dev,
				      struct scatterlist *sglist,
				      int nelems, int dir)
{
}

static inline void debug_dma_alloc_coherent(struct device *dev, size_t size,
					    dma_addr_t dma_addr, void *virt,
					    unsigned long attrs)
{
}

static inline void debug_dma_free_coherent(struct device *dev, size_t size,
					   void *virt, dma_addr_t addr)
{
}

static inline void debug_dma_map_resource(struct device *dev, phys_addr_t addr,
					  size_t size, int direction,
					  dma_addr_t dma_addr,
					  unsigned long attrs)
{
}

static inline void debug_dma_unmap_resource(struct device *dev,
					    dma_addr_t dma_addr, size_t size,
					    int direction)
{
}

static inline void debug_dma_sync_single_for_cpu(struct device *dev,
						 dma_addr_t dma_handle,
						 size_t size, int direction)
{
}

static inline void debug_dma_sync_single_for_device(struct device *dev,
						    dma_addr_t dma_handle,
						    size_t size, int direction)
{
}

static inline void debug_dma_sync_sg_for_cpu(struct device *dev,
					     struct scatterlist *sg,
					     int nelems, int direction)
{
}

static inline void debug_dma_sync_sg_for_device(struct device *dev,
						struct scatterlist *sg,
						int nelems, int direction)
{
}
#endif /* _KERNEL_DMA_DEBUG_H */
