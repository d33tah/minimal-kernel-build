/* module.h, interrupt.h, swab.h, ctype.h, kallsyms.h removed - unused */
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/bit_spinlock.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include "slab.h"
/* proc_fs.h, seq_file.h, cpuset.h, mempolicy.h, stackdepot.h, math64.h, stacktrace.h, sort.h, memcontrol.h removed - unused */
#include <linux/cpu.h>

#include "internal.h"

/* slub_dbg debug function removed - not needed for production */

#define slub_get_cpu_ptr(var) get_cpu_ptr(var)
#define slub_put_cpu_ptr(var) put_cpu_ptr(var)

/* fixup_red_left removed - always returned p unchanged (no red zone) */

#define MIN_PARTIAL 5

#define MAX_PARTIAL 10
/* DEBUG_DEFAULT_FLAGS removed - unused */
#define SLAB_NO_CMPXCHG (SLAB_CONSISTENCY_CHECKS | SLAB_STORE_USER | SLAB_TRACE)
/* DEBUG_METADATA_FLAGS removed - never used */

#define OO_SHIFT 16
#define OO_MASK ((1 << OO_SHIFT) - 1)
#define MAX_OBJS_PER_PAGE 32767
/* __OBJECT_POISON removed - never used */

#define __CMPXCHG_DOUBLE ((slab_flags_t __force)0x40000000U)
/* TRACK_ADDRS_COUNT, struct track, enum track_item removed - unused */
/* Removed: sysfs_slab_add, sysfs_slab_alias, debugfs_slab_add - empty stubs */

/* slab_nodes removed - only written, never read */

/* freelist_ptr inlined - always returned ptr unchanged (no obfuscation) */

static inline void *get_freepointer(struct kmem_cache *s, void *object)
{
	return *(void **)(object + s->offset);
}

static inline void set_freepointer(struct kmem_cache *s, void *object, void *fp)
{
	*(void **)((unsigned long)object + s->offset) = fp;
}

/* fixup_red_left removed - always returned p unchanged (no red zone) */
#define for_each_object(__p, __s, __addr, __objects)                     \
	for (__p = (__addr); __p < (__addr) + (__objects) * (__s)->size; \
	     __p += (__s)->size)

static inline unsigned int order_objects(unsigned int order, unsigned int size)
{
	return ((unsigned int)PAGE_SIZE << order) / size;
}

static inline struct kmem_cache_order_objects oo_make(unsigned int order,
						      unsigned int size)
{
	struct kmem_cache_order_objects x = { (order << OO_SHIFT) +
					      order_objects(order, size) };

	return x;
}

static inline unsigned int oo_order(struct kmem_cache_order_objects x)
{
	return x.x >> OO_SHIFT;
}

static inline unsigned int oo_objects(struct kmem_cache_order_objects x)
{
	return x.x & OO_MASK;
}

/* __slab_lock/__slab_unlock inlined into slab_lock/slab_unlock */
/* slab_lock inlined into __cmpxchg_double_slab */

static __always_inline void slab_unlock(struct slab *slab, unsigned long *flags)
{
	__bit_spin_unlock(PG_locked, &slab_page(slab)->flags);
	/* CONFIG_PREEMPT_RT not enabled - no irq restore */
}

static inline bool __cmpxchg_double_slab(struct kmem_cache *s,
					 struct slab *slab, void *freelist_old,
					 unsigned long counters_old,
					 void *freelist_new,
					 unsigned long counters_new,
					 const char *n)
{
	/* CONFIG_HAVE_CMPXCHG_DOUBLE block removed - not enabled in tinyconfig */
	unsigned long flags = 0;

	bit_spin_lock(PG_locked, &slab_page(slab)->flags);
	if (slab->freelist == freelist_old && slab->counters == counters_old) {
		slab->freelist = freelist_new;
		slab->counters = counters_new;
		slab_unlock(slab, &flags);
		return true;
	}
	slab_unlock(slab, &flags);

	cpu_relax();
	return false;
}

/* Removed: setup_object_debug, setup_slab_debug - empty stubs */

/* kmem_cache_flags removed - just returned flags unchanged */
#define slub_debug 0

#define disable_higher_order_debug 0

/* slabs_node and inc_slabs_node removed - always return 0 / do nothing */
/* Removed: kfree_hook, slab_free_hook, slab_free_freelist_hook
 * - No longer needed since kfree/kmem_cache_free are no-ops */

/* setup_object removed - inlined into 2 callers (~5 LOC) */
#define setup_object(s, obj)             \
	({                               \
		if (unlikely((s)->ctor)) \
			(s)->ctor(obj);  \
		(obj);                   \
	})

static inline struct slab *alloc_slab_page(gfp_t flags, int node,
					   struct kmem_cache_order_objects oo)
{
	struct folio *folio;
	struct slab *slab;
	unsigned int order = oo_order(oo);

	if (node == NUMA_NO_NODE)
		folio = (struct folio *)alloc_pages(flags, order);
	else
		folio = (struct folio *)__alloc_pages_node(node, flags, order);

	if (!folio)
		return NULL;

	slab = folio_slab(folio);
	__folio_set_slab(folio);
	/* page_is_pfmemalloc inlined - single caller */
	if ((uintptr_t)folio_page(folio, 0)->lru.next & BIT(1))
		folio_set_active(slab_folio(slab));

	return slab;
}

/* Removed: init_cache_random_seq, init_freelist_randomization
 * - Slab freelist randomization disabled for minimal kernel */

/* allocate_slab inlined into new_slab */

static struct slab *new_slab(struct kmem_cache *s, gfp_t flags, int node)
{
	struct slab *slab;
	struct kmem_cache_order_objects oo = s->oo;
	gfp_t alloc_gfp;
	void *start, *p, *next;
	int idx;

	if (unlikely(flags & GFP_SLAB_BUG_MASK))
		flags = kmalloc_fix_flags(flags);

	WARN_ON_ONCE(s->ctor && (flags & __GFP_ZERO));

	flags = (flags & (GFP_RECLAIM_MASK | GFP_CONSTRAINT_MASK)) &
		gfp_allowed_mask;
	flags |= s->allocflags;

	alloc_gfp = (flags | __GFP_NOWARN | __GFP_NORETRY) & ~__GFP_NOFAIL;
	if ((alloc_gfp & __GFP_DIRECT_RECLAIM) &&
	    oo_order(oo) > oo_order(s->min))
		alloc_gfp = (alloc_gfp | __GFP_NOMEMALLOC) & ~__GFP_RECLAIM;

	slab = alloc_slab_page(alloc_gfp, node, oo);
	if (unlikely(!slab)) {
		oo = s->min;
		alloc_gfp = flags;

		slab = alloc_slab_page(alloc_gfp, node, oo);
		if (unlikely(!slab))
			return NULL;
	}

	slab->objects = oo_objects(oo);

	/* account_slab and cache_vmstat_idx inlined - single caller */
	mod_node_page_state(folio_pgdat(slab_folio(slab)),
			    (s->flags & SLAB_RECLAIM_ACCOUNT) ?
				    NR_SLAB_RECLAIMABLE_B :
				    NR_SLAB_UNRECLAIMABLE_B,
			    PAGE_SIZE << oo_order(oo));

	slab->slab_cache = s;

	start = page_address(
		&slab_folio(slab)->page); /* folio_address inlined */
	start = setup_object(s, start);
	slab->freelist = start;
	for (idx = 0, p = start; idx < slab->objects - 1; idx++) {
		next = p + s->size;
		next = setup_object(s, next);
		set_freepointer(s, p, next);
		p = next;
	}
	set_freepointer(s, p, NULL);

	slab->inuse = slab->objects;
	slab->frozen = 1;

	return slab;
}

/* __add_partial inlined into early_kmem_cache_node_alloc */
/* add_partial and remove_partial inlined into callers */

static inline void *acquire_slab(struct kmem_cache *s,
				 struct kmem_cache_node *n, struct slab *slab,
				 int mode)
{
	void *freelist;
	unsigned long counters;
	struct slab new;

	freelist = slab->freelist;
	counters = slab->counters;
	new.counters = counters;
	if (mode) {
		new.inuse = slab->objects;
		new.freelist = NULL;
	} else {
		new.freelist = freelist;
	}

	new.frozen = 1;

	if (!__cmpxchg_double_slab(s, slab, freelist, counters, new.freelist,
				   new.counters, "acquire_slab"))
		return NULL;

	list_del(&slab->slab_list);
	n->nr_partial--;
	WARN_ON(!freelist);
	return freelist;
}

static void *get_partial_node(struct kmem_cache *s, struct kmem_cache_node *n,
			      struct slab **ret_slab, gfp_t gfpflags)
{
	struct slab *slab, *slab2;
	void *object = NULL;
	unsigned long flags;

	if (!n || !n->nr_partial)
		return NULL;

	spin_lock_irqsave(&n->list_lock, flags);
	list_for_each_entry_safe(slab, slab2, &n->partial, slab_list) {
		void *t;

		/* pfmemalloc_match inlined */
		if (unlikely(folio_test_active(
			    (struct folio *)slab_folio(slab))) &&
		    !gfp_pfmemalloc_allowed(gfpflags))
			continue;

		t = acquire_slab(s, n, slab, true);
		if (!t)
			break;

		*ret_slab = slab;
		object = t;
		break;
	}
	spin_unlock_irqrestore(&n->list_lock, flags);
	return object;
}

/* get_any_partial removed - always returned NULL */
/* get_partial inlined into ___slab_alloc */

#define TID_STEP 1

static inline unsigned long next_tid(unsigned long tid)
{
	return tid + TID_STEP;
}

/* init_tid, __flush_cpu_slab inlined */
/* slub_flush_work struct, flush_cpu_slab removed - no CPU hotplug */

static DEFINE_MUTEX(flush_lock);

/* flush_all_cpus_locked inlined into __kmem_cache_shutdown */
/* slub_cpu_dead removed - CPU never goes offline in single-CPU kernel (~10 LOC) */

/* node_match removed - always returned 1 */
/* slab_out_of_memory removed - was empty stub */
/* pfmemalloc_match inlined into get_partial_node (~7 LOC) */
/* get_freelist removed - never called (~19 LOC) */
/* ___slab_alloc inlined into slab_alloc_node */

static __always_inline void *slab_alloc_node(struct kmem_cache *s,
					     struct list_lru *lru,
					     gfp_t gfpflags, int node,
					     size_t orig_size)
{
	void *object;
	struct kmem_cache_cpu *c;
	struct slab *slab;
	unsigned long tid;
	struct obj_cgroup *objcg = NULL;
	bool init = false;

	s = slab_pre_alloc_hook(s, lru, &objcg, 1, gfpflags);
	if (!s)
		return NULL;

redo:

	c = raw_cpu_ptr(s->cpu_slab);
	tid = READ_ONCE(c->tid);

	barrier();

	object = c->freelist;
	slab = c->slab;
	/* CONFIG_PREEMPT_RT not enabled, node_match always returns 1 */
	if (unlikely(!object || !slab)) {
		/* ___slab_alloc inlined */
		void *freelist;
		int searchnode = (node == NUMA_NO_NODE) ? numa_mem_id() : node;
		freelist = get_partial_node(s, get_node(s, searchnode), &slab,
					    gfpflags);
		if (!freelist) {
			slub_put_cpu_ptr(s->cpu_slab);
			slab = new_slab(s, gfpflags, node);
			slub_get_cpu_ptr(s->cpu_slab);
			if (slab) {
				freelist = slab->freelist;
				slab->freelist = NULL;
			}
		}
		object = freelist;
	} else {
		void *next_object = get_freepointer(s, object);

		if (unlikely(!this_cpu_cmpxchg_double(
			    s->cpu_slab->freelist, s->cpu_slab->tid, object,
			    tid, next_object, next_tid(tid))))
			goto redo;
		prefetchw(next_object + s->offset);
	}

	/* maybe_wipe_obj_freeptr + slab_want_init_on_free inlined */
	if (static_branch_maybe(CONFIG_INIT_ON_FREE_DEFAULT_ON,
				&init_on_free) &&
	    !(s->ctor || (s->flags & (SLAB_TYPESAFE_BY_RCU | SLAB_POISON))) &&
	    object)
		memset((void *)((char *)object + s->offset), 0, sizeof(void *));

	/* slab_want_init_on_alloc inlined */
	if (static_branch_maybe(CONFIG_INIT_ON_ALLOC_DEFAULT_ON,
				&init_on_alloc)) {
		if (!s->ctor &&
		    !(s->flags & (SLAB_TYPESAFE_BY_RCU | SLAB_POISON)))
			init = true;
		else if (s->flags & (SLAB_TYPESAFE_BY_RCU | SLAB_POISON))
			init = gfpflags & __GFP_ZERO;
		else
			init = false;
	} else {
		init = gfpflags & __GFP_ZERO;
	}

	/* slab_post_alloc_hook inlined */
	if (object && init)
		memset(object, 0, s->object_size);

	return object;
}

static __always_inline void *slab_alloc(struct kmem_cache *s,
					struct list_lru *lru, gfp_t gfpflags,
					size_t orig_size)
{
	return slab_alloc_node(s, lru, gfpflags, NUMA_NO_NODE, orig_size);
}

static __always_inline void *__kmem_cache_alloc_lru(struct kmem_cache *s,
						    struct list_lru *lru,
						    gfp_t gfpflags)
{
	void *ret = slab_alloc(s, lru, gfpflags, s->object_size);

	return ret;
}

void *kmem_cache_alloc(struct kmem_cache *s, gfp_t gfpflags)
{
	return __kmem_cache_alloc_lru(s, NULL, gfpflags);
}

void *kmem_cache_alloc_lru(struct kmem_cache *s, struct list_lru *lru,
			   gfp_t gfpflags)
{
	return __kmem_cache_alloc_lru(s, lru, gfpflags);
}

/* kmem_cache_free, kfree moved to slab.h as static inline */

static unsigned int slub_min_order;
static unsigned int slub_max_order = PAGE_ALLOC_COSTLY_ORDER;
static unsigned int slub_min_objects;

static inline unsigned int calc_slab_order(unsigned int size,
					   unsigned int min_objects,
					   unsigned int max_order,
					   unsigned int fract_leftover)
{
	unsigned int min_order = slub_min_order;
	unsigned int order;

	if (order_objects(min_order, size) > MAX_OBJS_PER_PAGE)
		return get_order(size * MAX_OBJS_PER_PAGE) - 1;

	for (order = max(min_order,
			 (unsigned int)get_order(min_objects * size));
	     order <= max_order; order++) {
		unsigned int slab_size = (unsigned int)PAGE_SIZE << order;
		unsigned int rem;

		rem = slab_size % size;

		if (rem <= slab_size / fract_leftover)
			break;
	}

	return order;
}

static inline int calculate_order(unsigned int size)
{
	unsigned int order;
	unsigned int min_objects;
	unsigned int max_objects;

	min_objects = slub_min_objects;
	if (!min_objects)
		min_objects = 8; /* 4 * (fls(1) + 1), single CPU kernel */
	max_objects = order_objects(slub_max_order, size);
	min_objects = min(min_objects, max_objects);

	while (min_objects > 1) {
		unsigned int fraction;

		fraction = 16;
		while (fraction >= 4) {
			order = calc_slab_order(size, min_objects,
						slub_max_order, fraction);
			if (order <= slub_max_order)
				return order;
			fraction /= 2;
		}
		min_objects--;
	}

	order = calc_slab_order(size, 1, slub_max_order, 1);
	if (order <= slub_max_order)
		return order;

	order = calc_slab_order(size, 1, MAX_ORDER, 1);
	if (order < MAX_ORDER)
		return order;
	return -ENOSYS;
}

static void init_kmem_cache_node(struct kmem_cache_node *n)
{
	n->nr_partial = 0;
	spin_lock_init(&n->list_lock);
	INIT_LIST_HEAD(&n->partial);
}

/* alloc_kmem_cache_cpus inlined into __kmem_cache_create */

static struct kmem_cache *kmem_cache_node;

/* early_kmem_cache_node_alloc inlined into init_kmem_cache_nodes */

static void free_kmem_cache_nodes(struct kmem_cache *s)
{
	int node;
	struct kmem_cache_node *n;

	for_each_kmem_cache_node(s, node, n)
	{
		s->node[node] = NULL;
		kmem_cache_free(kmem_cache_node, n);
	}
}

static void __kmem_cache_release(struct kmem_cache *s)
{
	/* free_percpu removed - empty stub */
	free_kmem_cache_nodes(s);
}

/* init_kmem_cache_nodes inlined into kmem_cache_open */

static int kmem_cache_open(struct kmem_cache *s, slab_flags_t flags)
{
	unsigned int size = s->object_size;
	unsigned int order;

	s->flags = flags;

	/* calculate_sizes inlined */
	size = ALIGN(size, sizeof(void *));
	s->inuse = size;

	if ((flags & (SLAB_TYPESAFE_BY_RCU | SLAB_POISON)) ||
	    ((flags & SLAB_RED_ZONE) && s->object_size < sizeof(void *)) ||
	    s->ctor) {
		s->offset = size;
		size += sizeof(void *);
	} else {
		s->offset = ALIGN_DOWN(s->object_size / 2, sizeof(void *));
	}

	size = ALIGN(size, s->align);
	s->size = size;
	s->reciprocal_size = reciprocal_value(size);
	order = calculate_order(size);

	if ((int)order < 0)
		goto error;

	s->allocflags = 0;
	if (order)
		s->allocflags |= __GFP_COMP;

	if (flags & SLAB_CACHE_DMA)
		s->allocflags |= GFP_DMA;

	if (flags & SLAB_CACHE_DMA32)
		s->allocflags |= GFP_DMA32;

	if (flags & SLAB_RECLAIM_ACCOUNT)
		s->allocflags |= __GFP_RECLAIMABLE;

	s->oo = oo_make(order, size);
	s->min = oo_make(get_order(size), size);

	if (!oo_objects(s->oo))
		goto error;

	/* CONFIG_HAVE_CMPXCHG_DOUBLE block removed - not enabled in tinyconfig */

	s->min_partial = min_t(unsigned long, MAX_PARTIAL, ilog2(s->size) / 2);
	s->min_partial = max_t(unsigned long, MIN_PARTIAL, s->min_partial);

	/* init_kmem_cache_nodes inlined */
	if (slab_state == DOWN) {
		struct slab *slab;
		struct kmem_cache_node *n;
		BUG_ON(kmem_cache_node->size < sizeof(struct kmem_cache_node));
		slab = new_slab(kmem_cache_node, GFP_NOWAIT, 0);
		BUG_ON(!slab);
		if (page_to_nid(&slab_folio(slab)->page) != 0) {
			pr_err("SLUB: Unable to allocate memory from node %d\n",
			       0);
			pr_err("SLUB: Allocating a useless per node structure\n");
		}
		n = slab->freelist;
		BUG_ON(!n);
		slab->freelist = get_freepointer(kmem_cache_node, n);
		slab->inuse = 1;
		slab->frozen = 0;
		kmem_cache_node->node[0] = n;
		init_kmem_cache_node(n);
		n->nr_partial++;
		list_add(&slab->slab_list, &n->partial);
	} else {
		struct kmem_cache_node *n =
			kmem_cache_alloc_node(kmem_cache_node, GFP_KERNEL, 0);
		if (!n) {
			free_kmem_cache_nodes(s);
			goto error;
		}
		init_kmem_cache_node(n);
		s->node[0] = n;
	}

	/* alloc_kmem_cache_cpus inlined */
	BUILD_BUG_ON(PERCPU_DYNAMIC_EARLY_SIZE <
		     KMALLOC_SHIFT_HIGH * sizeof(struct kmem_cache_cpu));
	s->cpu_slab = __alloc_percpu(sizeof(struct kmem_cache_cpu),
				     2 * sizeof(void *));
	if (s->cpu_slab) {
		struct kmem_cache_cpu *c = per_cpu_ptr(s->cpu_slab, 0);
		c->tid = 0;
		return 0;
	}

error:
	__kmem_cache_release(s);
	return -EINVAL;
}

/* __kmem_cache_shutdown removed - never called */

/* setup_slub_min_order, setup_slub_max_order, setup_slub_min_objects and __setup
 * handlers removed - not needed for minimal kernel (~21 LOC) */

void *__kmalloc(size_t size, gfp_t flags)
{
	struct kmem_cache *s;
	void *ret;

	if (unlikely(size > KMALLOC_MAX_CACHE_SIZE))
		return kmalloc_large(size, flags);

	s = kmalloc_slab(size, flags);

	if (unlikely(ZERO_OR_NULL_PTR(s)))
		return s;

	ret = slab_alloc(s, NULL, flags, size);

	return ret;
}

/* __ksize removed - never called */

/* kfree moved to slab.h as static inline */

/* Memory hotplug callbacks removed - not needed for minimal kernel
 * (register_hotmemory_notifier is already a no-op) */

static struct kmem_cache *__init bootstrap(struct kmem_cache *static_cache)
{
	int node;
	struct kmem_cache *s = kmem_cache_zalloc(kmem_cache, GFP_NOWAIT);
	struct kmem_cache_node *n;

	memcpy(s, static_cache, kmem_cache->object_size);

	{
		struct kmem_cache_cpu *c =
			per_cpu_ptr(s->cpu_slab, smp_processor_id());
		c->slab = NULL;
		c->freelist = NULL;
		c->tid = next_tid(c->tid);
	}
	for_each_kmem_cache_node(s, node, n)
	{
		struct slab *p;

		list_for_each_entry(p, &n->partial, slab_list)
			p->slab_cache = s;
	}
	list_add(&s->list, &slab_caches);
	return s;
}

void __init kmem_cache_init(void)
{
	static __initdata struct kmem_cache boot_kmem_cache,
		boot_kmem_cache_node;

	/* debug_guardpage_minorder() always returns 0 - removed check */

	/* no_hash_pointers_enable call removed - slub debug disabled */

	kmem_cache_node = &boot_kmem_cache_node;
	kmem_cache = &boot_kmem_cache;

	/* for_each_node_state, node_set removed - slab_nodes was never read */

	create_boot_cache(kmem_cache_node, "kmem_cache_node",
			  sizeof(struct kmem_cache_node), SLAB_HWCACHE_ALIGN, 0,
			  0);

	/* Memory hotplug notifier removed - not needed for minimal kernel */

	slab_state = PARTIAL;

	create_boot_cache(kmem_cache, "kmem_cache",
			  offsetof(struct kmem_cache, node) +
				  nr_node_ids *
					  sizeof(struct kmem_cache_node *),
			  SLAB_HWCACHE_ALIGN, 0, 0);

	kmem_cache = bootstrap(&boot_kmem_cache);
	kmem_cache_node = bootstrap(&boot_kmem_cache_node);

	setup_kmalloc_cache_index_table();
	create_kmalloc_caches(0);
	/* cpuhp_setup_state_nocalls for slub_cpu_dead removed - CPU never goes offline */
}

/* kmem_cache_init_late removed - empty function */

/* __kmem_cache_alias removed - always returned NULL (slab_nomerge is true) */

int __kmem_cache_create(struct kmem_cache *s, slab_flags_t flags)
{
	int err;

	err = kmem_cache_open(s, flags);
	if (err)
		return err;

	/* sysfs_slab_add, debugfs_slab_add removed - empty stubs */
	return 0;
}

/* Stub: __kmalloc_track_caller not used in minimal kernel */
void *__kmalloc_track_caller(size_t size, gfp_t gfpflags, unsigned long caller)
{
	return __kmalloc(size, gfpflags);
}
