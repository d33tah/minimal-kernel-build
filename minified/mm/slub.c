
#include <linux/mm.h>
#include "slab.h"

#include "internal.h"

/* SLAB_NO_CMPXCHG removed - its bits (CONSISTENCY_CHECKS/STORE_USER/TRACE) are
 * never set on any cache, so the kmem_cache_open mask test folded to always-true */

/* DEBUG_METADATA_FLAGS removed - only consumer was the dead
 * disable_higher_order_debug (const 0) branch in kmem_cache_open */

#define OO_SHIFT 16
#define OO_MASK ((1 << OO_SHIFT) - 1)

static nodemask_t slab_nodes;

static inline void *get_freepointer(struct kmem_cache *s, void *object)
{
	return *(void **)(object + s->offset);
}

static inline void set_freepointer(struct kmem_cache *s, void *object, void *fp)
{
	unsigned long freeptr_addr = (unsigned long)object + s->offset;

	*(void **)freeptr_addr = fp;
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

static void *setup_object(struct kmem_cache *s, void *object)
{
	if (unlikely(s->ctor)) {
		s->ctor(object);
	}
	return object;
}

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

	return slab;
}

static struct slab *allocate_slab(struct kmem_cache *s, gfp_t flags, int node)
{
	struct slab *slab;
	struct kmem_cache_order_objects oo = s->oo;
	gfp_t alloc_gfp;
	void *start, *p, *next;
	int idx;

	flags &= gfp_allowed_mask;

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
			goto out;
	}

	slab->objects = oo_objects(oo);

	slab->slab_cache = s;

	start = slab_address(slab);

	{
		start = setup_object(s, start);
		slab->freelist = start;
		for (idx = 0, p = start; idx < slab->objects - 1; idx++) {
			next = p + s->size;
			next = setup_object(s, next);
			set_freepointer(s, p, next);
			p = next;
		}
		set_freepointer(s, p, NULL);
	}

	slab->inuse = 0;

out:
	if (!slab)
		return NULL;

	return slab;
}

static struct slab *new_slab(struct kmem_cache *s, gfp_t flags, int node)
{
	if (unlikely(flags & GFP_SLAB_BUG_MASK))
		flags = kmalloc_fix_flags(flags);

	return allocate_slab(
		s, flags & (GFP_RECLAIM_MASK | GFP_CONSTRAINT_MASK), node);
}

static inline void __add_partial(struct kmem_cache_node *n, struct slab *slab,
				 int tail)
{
	n->nr_partial++;
	if (tail == DEACTIVATE_TO_TAIL)
		list_add_tail(&slab->slab_list, &n->partial);
	else
		list_add(&slab->slab_list, &n->partial);
}

/*
 * One CPU, so there is no per-cpu slab: every cache keeps the slabs that
 * still have free objects on its node's partial list. Allocation pops an
 * object from the first partial slab (a new slab is added when the list is
 * empty) and drops the slab from the list once it is full; free pushes the
 * object back and puts a previously full slab back on the list.
 */
static __always_inline void *slab_alloc_node(struct kmem_cache *s,
					     gfp_t gfpflags, int node,
					     unsigned long addr,
					     size_t orig_size)
{
	struct kmem_cache_node *n;
	struct obj_cgroup *objcg = NULL;
	struct slab *slab;
	unsigned long flags;
	void *object;

	s = slab_pre_alloc_hook(s, &objcg, 1, gfpflags);
	if (!s)
		return NULL;

	if (node == NUMA_NO_NODE)
		node = numa_mem_id();
	n = get_node(s, node);

	spin_lock_irqsave(&n->list_lock, flags);
	slab = list_first_entry_or_null(&n->partial, struct slab, slab_list);
	if (!slab) {
		spin_unlock_irqrestore(&n->list_lock, flags);
		slab = new_slab(s, gfpflags, node);
		if (!slab)
			return NULL;
		spin_lock_irqsave(&n->list_lock, flags);
		__add_partial(n, slab, DEACTIVATE_TO_HEAD);
	}
	object = slab->freelist;
	slab->freelist = get_freepointer(s, object);
	slab->inuse++;
	if (!slab->freelist) {
		list_del(&slab->slab_list);
		n->nr_partial--;
	}
	spin_unlock_irqrestore(&n->list_lock, flags);

	slab_post_alloc_hook(s, objcg, gfpflags, 1, &object,
			     slab_want_init_on_alloc(gfpflags, s));

	return object;
}

static __always_inline void *slab_alloc(struct kmem_cache *s, gfp_t gfpflags,
					unsigned long addr, size_t orig_size)
{
	return slab_alloc_node(s, gfpflags, NUMA_NO_NODE, addr, orig_size);
}

void *kmem_cache_alloc(struct kmem_cache *s, gfp_t gfpflags)
{
	return slab_alloc(s, gfpflags, _RET_IP_, s->object_size);
}

static void slab_free(struct kmem_cache *s, struct slab *slab, void *object)
{
	struct kmem_cache_node *n = get_node(s, slab_nid(slab));
	unsigned long flags;
	void *prior;

	spin_lock_irqsave(&n->list_lock, flags);
	prior = slab->freelist;
	set_freepointer(s, object, prior);
	slab->freelist = object;
	slab->inuse--;
	if (!prior)
		__add_partial(n, slab, DEACTIVATE_TO_TAIL);
	spin_unlock_irqrestore(&n->list_lock, flags);
}

void kmem_cache_free(struct kmem_cache *s, void *x)
{
	s = cache_from_obj(s, x);
	if (!s)
		return;
	slab_free(s, virt_to_slab(x), x);
}

static inline void free_large_kmalloc(struct folio *folio, void *object)
{
	unsigned int order = folio_order(folio);

	__free_pages(folio_page(folio, 0), order);
}

#define slub_max_order ((unsigned int)PAGE_ALLOC_COSTLY_ORDER)

static inline unsigned int calc_slab_order(unsigned int size,
					   unsigned int min_objects,
					   unsigned int max_order,
					   unsigned int fract_leftover)
{
	unsigned int order;

	for (order = get_order(min_objects * size); order <= max_order;
	     order++) {
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
	unsigned int order, min_objects, max_objects, nr_cpus;

	nr_cpus = num_present_cpus();
	if (nr_cpus <= 1)
		nr_cpus = nr_cpu_ids;
	min_objects = 4 * (fls(nr_cpus) + 1);
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

static struct kmem_cache *kmem_cache_node;

static void early_kmem_cache_node_alloc(int node)
{
	struct slab *slab;
	struct kmem_cache_node *n;

	BUG_ON(kmem_cache_node->size < sizeof(struct kmem_cache_node));

	slab = new_slab(kmem_cache_node, GFP_NOWAIT, node);

	BUG_ON(!slab);
	/*
	 * CONFIG_NUMA is off here: there is a single node, so slab_nid(slab)
	 * always equals node and the wrong-node diagnostic can never fire.
	 */

	n = slab->freelist;
	BUG_ON(!n);
	slab->freelist = get_freepointer(kmem_cache_node, n);
	slab->inuse = 1;
	kmem_cache_node->node[node] = n;
	init_kmem_cache_node(n);

	__add_partial(n, slab, DEACTIVATE_TO_HEAD);
}

static int init_kmem_cache_nodes(struct kmem_cache *s)
{
	int node;

	for_each_node_mask(node, slab_nodes) {
		struct kmem_cache_node *n;

		if (slab_state == DOWN) {
			early_kmem_cache_node_alloc(node);
			continue;
		}
		n = kmem_cache_alloc_node(kmem_cache_node, GFP_KERNEL, node);

		if (!n)
			return 0;

		init_kmem_cache_node(n);
		s->node[node] = n;
	}
	return 1;
}

static int calculate_sizes(struct kmem_cache *s)
{
	slab_flags_t flags = s->flags;
	unsigned int size = s->object_size;
	unsigned int order;

	size = ALIGN(size, sizeof(void *));

	/* SLAB_POISON/SLAB_RED_ZONE never reach any cache on this build (not in
	 * CACHE_CREATE_MASK, never passed) -> only the RCU/ctor arms survive. */
	if ((flags & SLAB_TYPESAFE_BY_RCU) || s->ctor) {
		s->offset = size;
		size += sizeof(void *);
	} else {
		s->offset = ALIGN_DOWN(s->object_size / 2, sizeof(void *));
	}

	size = ALIGN(size, s->align);
	s->size = size;
	order = calculate_order(size);

	if ((int)order < 0)
		return 0;

	s->allocflags = 0;
	if (order)
		s->allocflags |= __GFP_COMP;

	/* SLAB_CACHE_DMA/DMA32 never set (KMALLOC_DMA == KMALLOC_NORMAL, no DMA
	 * cache, no callsite passes them) -> both arms dead. */

	s->oo = oo_make(order, size);
	s->min = oo_make(get_order(size), size);

	return !!oo_objects(s->oo);
}

static int kmem_cache_open(struct kmem_cache *s, slab_flags_t flags)
{
	s->flags = flags;

	if (!calculate_sizes(s))
		return -EINVAL;

	if (!init_kmem_cache_nodes(s))
		return -EINVAL;

	return 0;
}

/* setup_slub_min_order, setup_slub_max_order, setup_slub_min_objects and __setup
 * handlers removed - not needed for minimal kernel. slub_min_order/min_objects
 * folded to 0 and slub_max_order to PAGE_ALLOC_COSTLY_ORDER (write-never). */

void *__kmalloc(size_t size, gfp_t flags)
{
	struct kmem_cache *s;
	void *ret;

	if (unlikely(size > KMALLOC_MAX_CACHE_SIZE))
		return kmalloc_large(size, flags);

	s = kmalloc_slab(size, flags);

	if (unlikely(ZERO_OR_NULL_PTR(s)))
		return s;

	ret = slab_alloc(s, flags, _RET_IP_, size);

	return ret;
}

void kfree(const void *x)
{
	struct folio *folio;
	struct slab *slab;
	void *object = (void *)x;

	if (unlikely(ZERO_OR_NULL_PTR(x)))
		return;

	folio = virt_to_folio(x);
	if (unlikely(!folio_test_slab(folio))) {
		free_large_kmalloc(folio, object);
		return;
	}
	slab = folio_slab(folio);
	slab_free(slab->slab_cache, slab, object);
}

/* Memory hotplug callbacks removed - not needed for minimal kernel
 * (register_hotmemory_notifier is already a no-op) */

static struct kmem_cache *__init bootstrap(struct kmem_cache *static_cache)
{
	int node;
	struct kmem_cache *s = kmem_cache_zalloc(kmem_cache, GFP_NOWAIT);
	struct kmem_cache_node *n;

	memcpy(s, static_cache, kmem_cache->object_size);

	for_each_kmem_cache_node(s, node, n)
	{
		struct slab *p;

		list_for_each_entry(p, &n->partial, slab_list)
			p->slab_cache = s;
	}
	return s;
}

void __init kmem_cache_init(void)
{
	static __initdata struct kmem_cache boot_kmem_cache,
		boot_kmem_cache_node;
	int node;

	kmem_cache_node = &boot_kmem_cache_node;
	kmem_cache = &boot_kmem_cache;

	for_each_node_state(node, N_NORMAL_MEMORY)
		node_set(node, slab_nodes);

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
}

int __kmem_cache_create(struct kmem_cache *s, slab_flags_t flags)
{
	int err;

	err = kmem_cache_open(s, flags);
	if (err)
		return err;

	return 0;
}
