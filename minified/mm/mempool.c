// SPDX-License-Identifier: GPL-2.0
/* Stubbed - mempools unused in minimal kernel */
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/mempool.h>

void mempool_exit(mempool_t *pool) {}
EXPORT_SYMBOL(mempool_exit);

void mempool_destroy(mempool_t *pool) {}
EXPORT_SYMBOL(mempool_destroy);

int mempool_init_node(mempool_t *pool, int min_nr, mempool_alloc_t *alloc_fn, mempool_free_t *free_fn, void *pool_data, gfp_t gfp_mask, int node_id) { return -ENOMEM; }
EXPORT_SYMBOL(mempool_init_node);

int mempool_init(mempool_t *pool, int min_nr, mempool_alloc_t *alloc_fn, mempool_free_t *free_fn, void *pool_data) { return -ENOMEM; }
EXPORT_SYMBOL(mempool_init);

mempool_t *mempool_create(int min_nr, mempool_alloc_t *alloc_fn, mempool_free_t *free_fn, void *pool_data) { return NULL; }
EXPORT_SYMBOL(mempool_create);

mempool_t *mempool_create_node(int min_nr, mempool_alloc_t *alloc_fn, mempool_free_t *free_fn, void *pool_data, gfp_t gfp_mask, int node_id) { return NULL; }
EXPORT_SYMBOL(mempool_create_node);

int mempool_resize(mempool_t *pool, int new_min_nr) { return -ENOMEM; }
EXPORT_SYMBOL(mempool_resize);

void *mempool_alloc(mempool_t *pool, gfp_t gfp_mask) { return NULL; }
EXPORT_SYMBOL(mempool_alloc);

void mempool_free(void *element, mempool_t *pool) {}
EXPORT_SYMBOL(mempool_free);

void *mempool_alloc_slab(gfp_t gfp_mask, void *pool_data) { return NULL; }
EXPORT_SYMBOL(mempool_alloc_slab);

void mempool_free_slab(void *element, void *pool_data) {}
EXPORT_SYMBOL(mempool_free_slab);

void *mempool_kmalloc(gfp_t gfp_mask, void *pool_data) { return NULL; }
EXPORT_SYMBOL(mempool_kmalloc);

void mempool_kfree(void *element, void *pool_data) {}
EXPORT_SYMBOL(mempool_kfree);

void *mempool_alloc_pages(gfp_t gfp_mask, void *pool_data) { return NULL; }
EXPORT_SYMBOL(mempool_alloc_pages);

void mempool_free_pages(void *element, void *pool_data) {}
EXPORT_SYMBOL(mempool_free_pages);
