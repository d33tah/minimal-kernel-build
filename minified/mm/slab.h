/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MM_SLAB_H
#define MM_SLAB_H

/*
 * Internal slab definitions
 */

/* Reuses the bits in struct page */
struct slab {
	unsigned long __page_flags;
	union {
		struct list_head slab_list;
		struct rcu_head rcu_head;
	};
	struct kmem_cache *slab_cache;
	void *freelist; /* array of free object indexes */
	unsigned inuse : 16;
	unsigned objects : 15;
	unsigned int __unused;
	atomic_t __page_refcount;
};

#define SLAB_MATCH(pg, sl) \
	static_assert(offsetof(struct page, pg) == offsetof(struct slab, sl))
SLAB_MATCH(flags, __page_flags);
SLAB_MATCH(compound_head, slab_list); /* Ensure bit 0 is clear */
SLAB_MATCH(rcu_head, rcu_head);
SLAB_MATCH(_refcount, __page_refcount);
#undef SLAB_MATCH
static_assert(sizeof(struct slab) <= sizeof(struct page));

#define folio_slab(folio)                                            \
	(_Generic((folio),                                           \
		 const struct folio *: (const struct slab *)(folio), \
		 struct folio *: (struct slab *)(folio)))

#define slab_folio(s)                                          \
	(_Generic((s),                                         \
		 const struct slab *: (const struct folio *)s, \
		 struct slab *: (struct folio *)s))

static inline void *slab_address(const struct slab *slab)
{
	return folio_address(slab_folio(slab));
}

static inline int slab_nid(const struct slab *slab)
{
	return folio_nid(slab_folio(slab));
}

enum slab_deactivate_mode { DEACTIVATE_TO_HEAD, DEACTIVATE_TO_TAIL };

struct kmem_cache_order_objects {
	unsigned int x;
};

/*
 * Common fields provided in kmem_cache by all slab allocators
 * This struct is either used directly by the allocator (SLOB)
 * or the allocator must include definitions for all fields
 * provided in kmem_cache_common in their definition of kmem_cache.
 *
 * Once we can do anonymous structs (C11 standard) we could put a
 * anonymous struct definition in these allocators so that the
 * separate allocations in the kmem_cache structure of SLAB and
 * SLUB is no longer needed.
 */
struct kmem_cache {
	unsigned int size, object_size, offset;
	struct kmem_cache_order_objects oo, min;
	gfp_t allocflags;
	unsigned int align; /* Alignment as calculated */
	struct kmem_cache_node *node[MAX_NUMNODES];
};

#include <linux/memcontrol.h>
#include <linux/sched/mm.h>

/*
 * State of the slab allocator.
 *
 * This is used to describe the states of the allocator during bootup.
 * Allocators use this to gradually bootstrap themselves. Most allocators
 * have the problem that the structures used for managing slab caches are
 * allocated from slab caches themselves.
 */
enum slab_state {
	DOWN, /* No slab functionality yet */
	PARTIAL, /* SLUB: kmem_cache_node available */
	UP,
};

extern enum slab_state slab_state;

/* The slab cache that manages slab cache information */
extern struct kmem_cache *kmem_cache;

/* A table of kmalloc cache names and sizes */
extern const struct kmalloc_info_struct {
	const char *name[NR_KMALLOC_TYPES];
	unsigned int size;
} kmalloc_info[];

/* Kmalloc array related functions */
void setup_kmalloc_cache_index_table(void);
void create_kmalloc_caches(slab_flags_t);

/* Find the kmalloc slab corresponding for a certain size */
struct kmem_cache *kmalloc_slab(size_t, gfp_t);

gfp_t kmalloc_fix_flags(gfp_t flags);

/* Functions provided by the slab allocators */
int __kmem_cache_create(struct kmem_cache *, slab_flags_t flags);

extern void create_boot_cache(struct kmem_cache *, const char *name,
			      unsigned int size, slab_flags_t flags,
			      unsigned int useroffset, unsigned int usersize);

static inline struct kmem_cache *slab_pre_alloc_hook(struct kmem_cache *s,
						     struct obj_cgroup **objcgp,
						     size_t size, gfp_t flags)
{
	flags &= gfp_allowed_mask;

	return s;
}

static inline void slab_post_alloc_hook(struct kmem_cache *s,
					struct obj_cgroup *objcg, gfp_t flags,
					size_t size, void **p, bool init)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (p[i] && init)
			memset(p[i], 0, s->object_size);
	}
}

/*
 * The slab lists for all objects.
 */
struct kmem_cache_node {
	spinlock_t list_lock;
	unsigned long nr_partial;
	struct list_head partial;
};

static inline struct kmem_cache_node *get_node(struct kmem_cache *s, int node)
{
	return s->node[node];
}

/*
 * Iterator over all nodes. The body will be executed for each node that has
 * a kmem_cache_node structure allocated (which is true for all online nodes)
 */
#define for_each_kmem_cache_node(__s, __node, __n)       \
	for (__node = 0; __node < nr_node_ids; __node++) \
		if ((__n = get_node(__s, __node)))

static inline bool slab_want_init_on_alloc(gfp_t flags, struct kmem_cache *c)
{
	return flags & __GFP_ZERO;
}

#endif
