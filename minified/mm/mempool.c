/* Minimal includes - all functions are stubs */
#include <linux/mempool.h>
#include <linux/errno.h>

void mempool_exit(mempool_t *pool) {}

void mempool_destroy(mempool_t *pool) {}

int mempool_init_node(mempool_t *pool, int min_nr, mempool_alloc_t *alloc_fn, mempool_free_t *free_fn, void *pool_data, gfp_t gfp_mask, int node_id) { return -ENOMEM; }

int mempool_init(mempool_t *pool, int min_nr, mempool_alloc_t *alloc_fn, mempool_free_t *free_fn, void *pool_data) { return -ENOMEM; }

mempool_t *mempool_create(int min_nr, mempool_alloc_t *alloc_fn, mempool_free_t *free_fn, void *pool_data) { return NULL; }

mempool_t *mempool_create_node(int min_nr, mempool_alloc_t *alloc_fn, mempool_free_t *free_fn, void *pool_data, gfp_t gfp_mask, int node_id) { return NULL; }

int mempool_resize(mempool_t *pool, int new_min_nr) { return -ENOMEM; }

void *mempool_alloc(mempool_t *pool, gfp_t gfp_mask) { return NULL; }

void mempool_free(void *element, mempool_t *pool) {}

/* mempool_alloc_slab, mempool_free_slab, mempool_kmalloc, mempool_kfree,
   mempool_alloc_pages, mempool_free_pages removed - unused */
