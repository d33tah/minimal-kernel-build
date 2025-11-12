// SPDX-License-Identifier: GPL-2.0-only
/* Stubbed - DMA pools unused in minimal kernel */
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>

struct dma_pool *dma_pool_create(const char *name, struct device *dev, size_t size, size_t align, size_t boundary) { return NULL; }
EXPORT_SYMBOL(dma_pool_create);

void dma_pool_destroy(struct dma_pool *pool) {}
EXPORT_SYMBOL(dma_pool_destroy);

void *dma_pool_alloc(struct dma_pool *pool, gfp_t mem_flags, dma_addr_t *handle) { return NULL; }
EXPORT_SYMBOL(dma_pool_alloc);

void dma_pool_free(struct dma_pool *pool, void *vaddr, dma_addr_t dma) {}
EXPORT_SYMBOL(dma_pool_free);

struct dma_pool *dmam_pool_create(const char *name, struct device *dev, size_t size, size_t align, size_t allocation) { return NULL; }
EXPORT_SYMBOL(dmam_pool_create);

void dmam_pool_destroy(struct dma_pool *pool) {}
EXPORT_SYMBOL(dmam_pool_destroy);
