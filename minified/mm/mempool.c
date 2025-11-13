// SPDX-License-Identifier: GPL-2.0
/* Stubbed - mempools unused in minimal kernel */
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/mempool.h>

void mempool_exit(mempool_t *pool) {}

void mempool_destroy(mempool_t *pool) {}

int mempool_init_node(mempool_t *pool, int min_nr, mempool_alloc_t *alloc_fn, mempool_free_t *free_fn, void *pool_data, gfp_t gfp_mask, int node_id) { return -ENOMEM; }

int mempool_init(mempool_t *pool, int min_nr, mempool_alloc_t *alloc_fn, mempool_free_t *free_fn, void *pool_data) { return -ENOMEM; }

mempool_t *mempool_create(int min_nr, mempool_alloc_t *alloc_fn, mempool_free_t *free_fn, void *pool_data) { return NULL; }

mempool_t *mempool_create_node(int min_nr, mempool_alloc_t *alloc_fn, mempool_free_t *free_fn, void *pool_data, gfp_t gfp_mask, int node_id) { return NULL; }

int mempool_resize(mempool_t *pool, int new_min_nr) { return -ENOMEM; }

void *mempool_alloc(mempool_t *pool, gfp_t gfp_mask) { return NULL; }

void mempool_free(void *element, mempool_t *pool) {}

void *mempool_alloc_slab(gfp_t gfp_mask, void *pool_data) { return NULL; }

void mempool_free_slab(void *element, void *pool_data) {}

void *mempool_kmalloc(gfp_t gfp_mask, void *pool_data) { return NULL; }

void mempool_kfree(void *element, void *pool_data) {}

void *mempool_alloc_pages(gfp_t gfp_mask, void *pool_data) { return NULL; }

void mempool_free_pages(void *element, void *pool_data) {}
