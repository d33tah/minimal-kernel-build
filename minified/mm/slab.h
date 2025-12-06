 
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

 
#define page_slab(p)		(_Generic((p),				\
	const struct page *:	(const struct slab *)(p),		\
	struct page *:		(struct slab *)(p)))

 
#define slab_page(s) folio_page(slab_folio(s), 0)

 
static inline bool slab_test_pfmemalloc(const struct slab *slab)
{
	return folio_test_active((struct folio *)slab_folio(slab));
}

static inline void slab_set_pfmemalloc(struct slab *slab)
{
	folio_set_active(slab_folio(slab));
}

static inline void slab_clear_pfmemalloc(struct slab *slab)
{
	folio_clear_active(slab_folio(slab));
}

static inline void __slab_clear_pfmemalloc(struct slab *slab)
{
	__folio_clear_active(slab_folio(slab));
}

static inline void *slab_address(const struct slab *slab)
{
	return folio_address(slab_folio(slab));
}

static inline int slab_nid(const struct slab *slab)
{
	return folio_nid(slab_folio(slab));
}

static inline pg_data_t *slab_pgdat(const struct slab *slab)
{
	return folio_pgdat(slab_folio(slab));
}

static inline struct slab *virt_to_slab(const void *addr)
{
	struct folio *folio = virt_to_folio(addr);

	if (!folio_test_slab(folio))
		return NULL;

	return folio_slab(folio);
}

static inline int slab_order(const struct slab *slab)
{
	return folio_order((struct folio *)slab_folio(slab));
}

static inline size_t slab_size(const struct slab *slab)
{
	return PAGE_SIZE << slab_order(slab);
}



/* --- 2025-12-06 17:00 --- slub_def.h inlined */
#include <linux/kfence.h>
#include <linux/kobject.h>
#include <linux/reciprocal_div.h>
#include <linux/local_lock.h>

enum stat_item {
	ALLOC_FASTPATH,
	ALLOC_SLOWPATH,
	FREE_FASTPATH,
	FREE_SLOWPATH,
	FREE_FROZEN,
	FREE_ADD_PARTIAL,
	FREE_REMOVE_PARTIAL,
	ALLOC_FROM_PARTIAL,
	ALLOC_SLAB,
	ALLOC_REFILL,
	ALLOC_NODE_MISMATCH,
	FREE_SLAB,
	CPUSLAB_FLUSH,
	DEACTIVATE_FULL,
	DEACTIVATE_EMPTY,
	DEACTIVATE_TO_HEAD,
	DEACTIVATE_TO_TAIL,
	DEACTIVATE_REMOTE_FREES,
	DEACTIVATE_BYPASS,
	ORDER_FALLBACK,
	CMPXCHG_DOUBLE_CPU_FAIL,
	CMPXCHG_DOUBLE_FAIL,
	CPU_PARTIAL_ALLOC,
	CPU_PARTIAL_FREE,
	CPU_PARTIAL_NODE,
	CPU_PARTIAL_DRAIN,
	NR_SLUB_STAT_ITEMS };

struct kmem_cache_cpu {
	void **freelist;
	unsigned long tid;
	struct slab *slab;
	local_lock_t lock;
};

#define slub_percpu_partial(c)			NULL
#define slub_set_percpu_partial(c, p)
#define slub_percpu_partial_read_once(c)	NULL

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

static inline void sysfs_slab_unlink(struct kmem_cache *s) { }
static inline void sysfs_slab_release(struct kmem_cache *s) { }

void *fixup_red_left(struct kmem_cache *s, void *p);

static inline void *nearest_obj(struct kmem_cache *cache, const struct slab *slab,
				void *x) {
	void *object = x - (x - slab_address(slab)) % cache->size;
	void *last_object = slab_address(slab) +
		(slab->objects - 1) * cache->size;
	void *result = (unlikely(object > last_object)) ? last_object : object;

	result = fixup_red_left(cache, result);
	return result;
}

static inline unsigned int __obj_to_index(const struct kmem_cache *cache,
					  void *addr, void *obj)
{
	return reciprocal_divide(kasan_reset_tag(obj) - addr,
				 cache->reciprocal_size);
}

static inline unsigned int obj_to_index(const struct kmem_cache *cache,
					const struct slab *slab, void *obj)
{
	if (is_kfence_address(obj))
		return 0;
	return __obj_to_index(cache, slab_address(slab), obj);
}

static inline int objs_per_slab(const struct kmem_cache *cache,
				     const struct slab *slab)
{
	return slab->objects;
}
/* --- end slub_def.h inlined --- */

#include <linux/memcontrol.h>
#include <linux/fault-inject.h>
#include <linux/kasan.h>
#include <linux/kmemleak.h>
#include <linux/random.h>
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

int slab_unmergeable(struct kmem_cache *s);
struct kmem_cache *find_mergeable(unsigned size, unsigned align,
		slab_flags_t flags, const char *name, void (*ctor)(void *));
struct kmem_cache *
__kmem_cache_alias(const char *name, unsigned int size, unsigned int align,
		   slab_flags_t flags, void (*ctor)(void *));

slab_flags_t kmem_cache_flags(unsigned int object_size,
	slab_flags_t flags, const char *name);


 
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

bool __kmem_cache_empty(struct kmem_cache *);
int __kmem_cache_shutdown(struct kmem_cache *);
void __kmem_cache_release(struct kmem_cache *);
int __kmem_cache_shrink(struct kmem_cache *);
void slab_kmem_cache_release(struct kmem_cache *);

struct seq_file;
struct file;

struct slabinfo {
	unsigned long active_objs;
	unsigned long num_objs;
	unsigned long active_slabs;
	unsigned long num_slabs;
	unsigned long shared_avail;
	unsigned int limit;
	unsigned int batchcount;
	unsigned int shared;
	unsigned int objects_per_slab;
	unsigned int cache_order;
};

void get_slabinfo(struct kmem_cache *s, struct slabinfo *sinfo);
void slabinfo_show_stats(struct seq_file *m, struct kmem_cache *s);
ssize_t slabinfo_write(struct file *file, const char __user *buffer,
		       size_t count, loff_t *ppos);

 
void __kmem_cache_free_bulk(struct kmem_cache *, size_t, void **);
int __kmem_cache_alloc_bulk(struct kmem_cache *, gfp_t, size_t, void **);

static inline enum node_stat_item cache_vmstat_idx(struct kmem_cache *s)
{
	return (s->flags & SLAB_RECLAIM_ACCOUNT) ?
		NR_SLAB_RECLAIMABLE_B : NR_SLAB_UNRECLAIMABLE_B;
}

static inline void print_tracking(struct kmem_cache *s, void *object)
{
}
static inline bool __slub_debug_enabled(void)
{
	return false;
}

 
static inline bool kmem_cache_debug_flags(struct kmem_cache *s, slab_flags_t flags)
{
	if (IS_ENABLED(CONFIG_SLUB_DEBUG))
		VM_WARN_ON_ONCE(!(flags & SLAB_DEBUG_FLAGS));
	if (__slub_debug_enabled())
		return s->flags & flags;
	return false;
}

static inline struct obj_cgroup **slab_objcgs(struct slab *slab)
{
	return NULL;
}

static inline struct mem_cgroup *memcg_from_slab_obj(void *ptr)
{
	return NULL;
}

static inline int memcg_alloc_slab_cgroups(struct slab *slab,
					       struct kmem_cache *s, gfp_t gfp,
					       bool new_slab)
{
	return 0;
}

static inline void memcg_free_slab_cgroups(struct slab *slab)
{
}

static inline bool memcg_slab_pre_alloc_hook(struct kmem_cache *s,
					     struct list_lru *lru,
					     struct obj_cgroup **objcgp,
					     size_t objects, gfp_t flags)
{
	return true;
}

static inline void memcg_slab_post_alloc_hook(struct kmem_cache *s,
					      struct obj_cgroup *objcg,
					      gfp_t flags, size_t size,
					      void **p)
{
}

static inline void memcg_slab_free_hook(struct kmem_cache *s,
					void **p, int objects)
{
}

static inline struct kmem_cache *virt_to_cache(const void *obj)
{
	struct slab *slab;

	slab = virt_to_slab(obj);
	if (WARN_ONCE(!slab, "%s: Object is not a Slab page!\n",
					__func__))
		return NULL;
	return slab->slab_cache;
}

static __always_inline void account_slab(struct slab *slab, int order,
					 struct kmem_cache *s, gfp_t gfp)
{
	if (memcg_kmem_enabled() && (s->flags & SLAB_ACCOUNT))
		memcg_alloc_slab_cgroups(slab, s, gfp, true);

	mod_node_page_state(slab_pgdat(slab), cache_vmstat_idx(s),
			    PAGE_SIZE << order);
}

static __always_inline void unaccount_slab(struct slab *slab, int order,
					   struct kmem_cache *s)
{
	if (memcg_kmem_enabled())
		memcg_free_slab_cgroups(slab);

	mod_node_page_state(slab_pgdat(slab), cache_vmstat_idx(s),
			    -(PAGE_SIZE << order));
}

static inline struct kmem_cache *cache_from_obj(struct kmem_cache *s, void *x)
{
	struct kmem_cache *cachep;

	if (!IS_ENABLED(CONFIG_SLAB_FREELIST_HARDENED) &&
	    !kmem_cache_debug_flags(s, SLAB_CONSISTENCY_CHECKS))
		return s;

	cachep = virt_to_cache(x);
	if (WARN(cachep && cachep != s,
		  "%s: Wrong slab cache. %s but object is from %s\n",
		  __func__, s->name, cachep->name))
		print_tracking(cachep, x);
	return cachep;
}

static inline size_t slab_ksize(const struct kmem_cache *s)
{
	if (s->flags & SLAB_KASAN)
		return s->object_size;
	 
	if (s->flags & (SLAB_TYPESAFE_BY_RCU | SLAB_STORE_USER))
		return s->inuse;
	 
	return s->size;
}

static inline struct kmem_cache *slab_pre_alloc_hook(struct kmem_cache *s,
						     struct list_lru *lru,
						     struct obj_cgroup **objcgp,
						     size_t size, gfp_t flags)
{
	flags &= gfp_allowed_mask;

	might_alloc(flags);

	if (should_failslab(s, flags))
		return NULL;

	if (!memcg_slab_pre_alloc_hook(s, lru, objcgp, size, flags))
		return NULL;

	return s;
}

static inline void slab_post_alloc_hook(struct kmem_cache *s,
					struct obj_cgroup *objcg, gfp_t flags,
					size_t size, void **p, bool init)
{
	size_t i;

	flags &= gfp_allowed_mask;

	 
	for (i = 0; i < size; i++) {
		p[i] = kasan_slab_alloc(s, p[i], flags, init);
		if (p[i] && init && !kasan_has_integrated_init())
			memset(p[i], 0, s->object_size);
		kmemleak_alloc_recursive(p[i], s->object_size, 1,
					 s->flags, flags);
	}

	memcg_slab_post_alloc_hook(s, objcg, flags, size, p);
}

 
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


static inline void dump_unreclaimable_slab(void)
{
}

void ___cache_free(struct kmem_cache *cache, void *x, unsigned long addr);

static inline int cache_random_seq_create(struct kmem_cache *cachep,
					unsigned int count, gfp_t gfp)
{
	return 0;
}
static inline void cache_random_seq_destroy(struct kmem_cache *cachep) { }

static inline bool slab_want_init_on_alloc(gfp_t flags, struct kmem_cache *c)
{
	if (static_branch_maybe(CONFIG_INIT_ON_ALLOC_DEFAULT_ON,
				&init_on_alloc)) {
		if (c->ctor)
			return false;
		if (c->flags & (SLAB_TYPESAFE_BY_RCU | SLAB_POISON))
			return flags & __GFP_ZERO;
		return true;
	}
	return flags & __GFP_ZERO;
}

static inline bool slab_want_init_on_free(struct kmem_cache *c)
{
	if (static_branch_maybe(CONFIG_INIT_ON_FREE_DEFAULT_ON,
				&init_on_free))
		return !(c->ctor ||
			 (c->flags & (SLAB_TYPESAFE_BY_RCU | SLAB_POISON)));
	return false;
}

static inline void debugfs_slab_release(struct kmem_cache *s) { }


void __check_heap_object(const void *ptr, unsigned long n,
			 const struct slab *slab, bool to_user);

#endif  
