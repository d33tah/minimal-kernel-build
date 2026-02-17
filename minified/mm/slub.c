#include <linux/mm.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include "slab.h"
#include <linux/cpu.h>

#include "internal.h"

#define MIN_PARTIAL 5

#define MAX_PARTIAL 10

#define OO_SHIFT 16
#define OO_MASK ((1 << OO_SHIFT) - 1)
#define MAX_OBJS_PER_PAGE 32767

static inline void *get_freepointer(struct kmem_cache *s, void *object)
{
	return *(void **)(object + s->offset);
}

static inline void set_freepointer(struct kmem_cache *s, void *object, void *fp)
{
	*(void **)((unsigned long)object + s->offset) = fp;
}

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
	if ((uintptr_t)folio_page(folio, 0)->lru.next & BIT(1))
		folio_set_active(slab_folio(slab));

	return slab;
}

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

#define TID_STEP 1

static inline unsigned long next_tid(unsigned long tid)
{
	return tid + TID_STEP;
}

static __always_inline void *slab_alloc_node(struct kmem_cache *s,
					     struct list_lru *lru,
					     gfp_t gfpflags, int node,
					     size_t orig_size)
{
	void *object;
	struct kmem_cache_cpu *c;
	struct slab *slab;
	struct obj_cgroup *objcg = NULL;

	s = slab_pre_alloc_hook(s, lru, &objcg, 1, gfpflags);
	if (!s)
		return NULL;

	c = raw_cpu_ptr(s->cpu_slab);
	object = c->freelist;
	slab = c->slab;

	if (likely(object && slab)) {
		c->freelist = get_freepointer(s, object);
		c->tid = next_tid(c->tid);
	} else {
		void *freelist;
		int searchnode = (node == NUMA_NO_NODE) ? numa_mem_id() : node;

		freelist = get_partial_node(s, get_node(s, searchnode), &slab,
					    gfpflags);
		if (!freelist) {
			slab = new_slab(s, gfpflags, node);
			if (slab) {
				freelist = slab->freelist;
				slab->freelist = NULL;
			}
		}
		object = freelist;
	}

	if (object && (gfpflags & __GFP_ZERO))
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

static inline int calculate_order(unsigned int size)
{
	unsigned int order;

	/* Simple: find smallest order that fits at least one object */
	for (order = 0; order <= MAX_ORDER; order++) {
		if (order_objects(order, size) >= 1)
			return order;
	}
	return -ENOSYS;
}

static void init_kmem_cache_node(struct kmem_cache_node *n)
{
	n->nr_partial = 0;
	spin_lock_init(&n->list_lock);
	INIT_LIST_HEAD(&n->partial);
}

static struct kmem_cache *kmem_cache_node;

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

static int kmem_cache_open(struct kmem_cache *s, slab_flags_t flags)
{
	unsigned int size = s->object_size;
	unsigned int order;

	s->flags = flags;

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

	s->min_partial = min_t(unsigned long, MAX_PARTIAL, ilog2(s->size) / 2);
	s->min_partial = max_t(unsigned long, MIN_PARTIAL, s->min_partial);

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
	free_kmem_cache_nodes(s);
	return -EINVAL;
}

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

	kmem_cache_node = &boot_kmem_cache_node;
	kmem_cache = &boot_kmem_cache;

	create_boot_cache(kmem_cache_node, "kmem_cache_node",
			  sizeof(struct kmem_cache_node), SLAB_HWCACHE_ALIGN, 0,
			  0);

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
}

int __kmem_cache_create(struct kmem_cache *s, slab_flags_t flags)
{
	int err;

	err = kmem_cache_open(s, flags);
	if (err)
		return err;

	return 0;
}

void *__kmalloc_track_caller(size_t size, gfp_t gfpflags, unsigned long caller)
{
	return __kmalloc(size, gfpflags);
}
