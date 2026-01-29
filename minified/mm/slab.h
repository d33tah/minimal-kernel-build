 
#ifndef MM_SLAB_H
#define MM_SLAB_H
 

 
struct slab {
	unsigned long __page_flags;


	union {
		struct list_head slab_list;
		struct rcu_head rcu_head;
	};
	struct kmem_cache *slab_cache;
	 
	void *freelist;		 
	union {
		unsigned long counters;
		struct {
			unsigned inuse:16;
			unsigned objects:15;
			unsigned frozen:1;
		};
	};
	unsigned int __unused;


	atomic_t __page_refcount;
};

#define SLAB_MATCH(pg, sl)						\
	static_assert(offsetof(struct page, pg) == offsetof(struct slab, sl))
SLAB_MATCH(flags, __page_flags);
SLAB_MATCH(compound_head, slab_list);	 
SLAB_MATCH(rcu_head, rcu_head);
SLAB_MATCH(_refcount, __page_refcount);
#undef SLAB_MATCH
static_assert(sizeof(struct slab) <= sizeof(struct page));

 
#define folio_slab(folio)	(_Generic((folio),			\
	const struct folio *:	(const struct slab *)(folio),		\
	struct folio *:		(struct slab *)(folio)))

 
#define slab_folio(s)		(_Generic((s),				\
	const struct slab *:	(const struct folio *)s,		\
	struct slab *:		(struct folio *)s))

/* page_slab removed - unused */

#define slab_page(s) folio_page(slab_folio(s), 0)

/* slab_test_pfmemalloc, slab_set_pfmemalloc removed - inlined at single call sites */
/* slab_clear_pfmemalloc, __slab_clear_pfmemalloc, slab_address, slab_nid removed - inlined */
/* slab_pgdat removed - inlined at single call site */
/* virt_to_slab, slab_order, slab_size removed - unused */



#include <linux/kobject.h>
#include <linux/reciprocal_div.h>
#include <linux/local_lock.h>

/* enum stat_item removed - SLUB statistics not used (~27 values) */

struct kmem_cache_cpu {
	void **freelist;
	unsigned long tid;
	struct slab *slab;
	local_lock_t lock;
};

#define slub_percpu_partial(c)			NULL
/* slub_set_percpu_partial and slub_percpu_partial_read_once removed - unused */

struct kmem_cache_order_objects {
	unsigned int x;
};

struct kmem_cache {
	struct kmem_cache_cpu __percpu *cpu_slab;
	slab_flags_t flags;
	unsigned long min_partial;
	unsigned int size;
	unsigned int object_size;
	struct reciprocal_value reciprocal_size;
	unsigned int offset;
	struct kmem_cache_order_objects oo;
	struct kmem_cache_order_objects min;
	gfp_t allocflags;
	int refcount;
	void (*ctor)(void *);
	unsigned int inuse;
	unsigned int align;
	unsigned int red_left_pad;
	const char *name;
	struct list_head list;
	unsigned int useroffset;
	unsigned int usersize;
	struct kmem_cache_node *node[MAX_NUMNODES];
};

/* sysfs_slab_unlink/release, fixup_red_left removed - unused */

/* nearest_obj, __obj_to_index, obj_to_index, objs_per_slab removed - unused */

#include <linux/memcontrol.h>
/* should_failslab, __should_failslab removed - always returns false */
/* end fault-inject.h */
/* kmemleak_alloc_recursive removed - unused */
/* random.h removed - header is empty */
#include <linux/sched/mm.h>
#include <linux/list_lru.h>

 
enum slab_state {
	DOWN,			 
	PARTIAL,		 
	PARTIAL_NODE,		 
	UP,			 
	FULL			 
};

extern enum slab_state slab_state;

 
extern struct mutex slab_mutex;

 
extern struct list_head slab_caches;

 
extern struct kmem_cache *kmem_cache;

 
extern const struct kmalloc_info_struct {
	const char *name[NR_KMALLOC_TYPES];
	unsigned int size;
} kmalloc_info[];

 
void setup_kmalloc_cache_index_table(void);
void create_kmalloc_caches(slab_flags_t);

 
struct kmem_cache *kmalloc_slab(size_t, gfp_t);

gfp_t kmalloc_fix_flags(gfp_t flags);

 
int __kmem_cache_create(struct kmem_cache *, slab_flags_t flags);

struct kmem_cache *create_kmalloc_cache(const char *name, unsigned int size,
			slab_flags_t flags, unsigned int useroffset,
			unsigned int usersize);
extern void create_boot_cache(struct kmem_cache *, const char *name,
			unsigned int size, slab_flags_t flags,
			unsigned int useroffset, unsigned int usersize);

/* find_mergeable, __kmem_cache_alias, kmem_cache_flags removed - unused */


 
#define SLAB_CORE_FLAGS (SLAB_HWCACHE_ALIGN | SLAB_CACHE_DMA | \
			 SLAB_CACHE_DMA32 | SLAB_PANIC | \
			 SLAB_TYPESAFE_BY_RCU | SLAB_DEBUG_OBJECTS )

#define SLAB_DEBUG_FLAGS (0)

#define SLAB_CACHE_FLAGS (SLAB_NOLEAKTRACE | SLAB_RECLAIM_ACCOUNT | \
			  SLAB_TEMPORARY | SLAB_ACCOUNT | SLAB_NO_USER_FLAGS)

 
#define CACHE_CREATE_MASK (SLAB_CORE_FLAGS | SLAB_DEBUG_FLAGS | SLAB_CACHE_FLAGS)

 
#define SLAB_FLAGS_PERMITTED (SLAB_CORE_FLAGS | \
			      SLAB_RED_ZONE | \
			      SLAB_POISON | \
			      SLAB_STORE_USER | \
			      SLAB_TRACE | \
			      SLAB_CONSISTENCY_CHECKS | \
			      SLAB_MEM_SPREAD | \
			      SLAB_NOLEAKTRACE | \
			      SLAB_RECLAIM_ACCOUNT | \
			      SLAB_TEMPORARY | \
			      SLAB_ACCOUNT | \
			      SLAB_NO_USER_FLAGS)

int __kmem_cache_shutdown(struct kmem_cache *);
void __kmem_cache_release(struct kmem_cache *);

/* cache_vmstat_idx and account_slab inlined at slub.c - single caller */

/* unaccount_slab, cache_from_obj, slab_ksize removed - unused/inlined */

static inline struct kmem_cache *slab_pre_alloc_hook(struct kmem_cache *s,
						     struct list_lru *lru,
						     struct obj_cgroup **objcgp,
						     size_t size, gfp_t flags)
{
	flags &= gfp_allowed_mask;

	might_alloc(flags);

	/* should_failslab, memcg_slab_pre_alloc_hook always return 0/true - checks removed */

	return s;
}

/* slab_post_alloc_hook inlined into slub.c - single caller */

 
struct kmem_cache_node {
	spinlock_t list_lock;


	unsigned long nr_partial;
	struct list_head partial;

};

static inline struct kmem_cache_node *get_node(struct kmem_cache *s, int node)
{
	return s->node[node];
}

 
#define for_each_kmem_cache_node(__s, __node, __n) \
	for (__node = 0; __node < nr_node_ids; __node++) \
		 if ((__n = get_node(__s, __node)))


/* dump_unreclaimable_slab, ___cache_free removed - unused */

/* slab_want_init_on_alloc, slab_want_init_on_free inlined into slub.c */

#endif  
