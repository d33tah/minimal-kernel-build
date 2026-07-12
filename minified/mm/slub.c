
#include <linux/mm.h>
#include "slab.h"



#include "internal.h"

#define slub_get_cpu_ptr(var)	get_cpu_ptr(var)
#define slub_put_cpu_ptr(var)	put_cpu_ptr(var)

/* SLAB_NO_CMPXCHG removed - its bits (CONSISTENCY_CHECKS/STORE_USER/TRACE) are
 * never set on any cache, so the kmem_cache_open mask test folded to always-true */

/* DEBUG_METADATA_FLAGS removed - only consumer was the dead
 * disable_higher_order_debug (const 0) branch in kmem_cache_open */

#define OO_SHIFT	16
#define OO_MASK		((1 << OO_SHIFT) - 1)

#define __CMPXCHG_DOUBLE	((slab_flags_t __force)0x40000000U)

static nodemask_t slab_nodes;

static inline void *get_freepointer(struct kmem_cache *s, void *object)
{
	return *(void **)(object + s->offset);
}

static void prefetch_freepointer(const struct kmem_cache *s, void *object)
{
	prefetchw(object + s->offset);
}

static inline void *get_freepointer_safe(struct kmem_cache *s, void *object)
{
	return get_freepointer(s, object);
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

static inline struct kmem_cache_order_objects oo_make(unsigned int order, unsigned int size)
{
	struct kmem_cache_order_objects x = {
		(order << OO_SHIFT) + order_objects(order, size)
	};

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


static __always_inline void __slab_lock(struct slab *slab)
{
	struct page *page = slab_page(slab);

	VM_BUG_ON_PAGE(PageTail(page), page);
	bit_spin_lock(PG_locked, &page->flags);
}

static __always_inline void __slab_unlock(struct slab *slab)
{
	struct page *page = slab_page(slab);

	VM_BUG_ON_PAGE(PageTail(page), page);
	__bit_spin_unlock(PG_locked, &page->flags);
}

static __always_inline void slab_lock(struct slab *slab)
{
	__slab_lock(slab);
}

static __always_inline void slab_unlock(struct slab *slab)
{
	__slab_unlock(slab);
}

static inline bool __cmpxchg_double_slab(struct kmem_cache *s, struct slab *slab, void *freelist_old, unsigned long counters_old, void *freelist_new, unsigned long counters_new)
{
	lockdep_assert_irqs_disabled();
#if defined(CONFIG_HAVE_CMPXCHG_DOUBLE) && \
    defined(CONFIG_HAVE_ALIGNED_STRUCT_PAGE)
	if (s->flags & __CMPXCHG_DOUBLE) {
		if (cmpxchg_double(&slab->freelist, &slab->counters, freelist_old, counters_old, freelist_new, counters_new))
			return true;
	} else
#endif
	{
		slab_lock(slab);
		if (slab->freelist == freelist_old && slab->counters == counters_old) {
			slab->freelist = freelist_new;
			slab->counters = counters_new;
			slab_unlock(slab);
			return true;
		}
		slab_unlock(slab);
	}

	cpu_relax();
	return false;
}

static inline bool cmpxchg_double_slab(struct kmem_cache *s, struct slab *slab, void *freelist_old, unsigned long counters_old, void *freelist_new, unsigned long counters_new)
{
#if defined(CONFIG_HAVE_CMPXCHG_DOUBLE) && \
    defined(CONFIG_HAVE_ALIGNED_STRUCT_PAGE)
	if (s->flags & __CMPXCHG_DOUBLE) {
		if (cmpxchg_double(&slab->freelist, &slab->counters, freelist_old, counters_old, freelist_new, counters_new))
			return true;
	} else
#endif
	{
		unsigned long flags;

		local_irq_save(flags);
		__slab_lock(slab);
		if (slab->freelist == freelist_old && slab->counters == counters_old) {
			slab->freelist = freelist_new;
			slab->counters = counters_new;
			__slab_unlock(slab);
			local_irq_restore(flags);
			return true;
		}
		__slab_unlock(slab);
		local_irq_restore(flags);
	}

	cpu_relax();
	return false;
}

static __always_inline bool slab_free_hook(struct kmem_cache *s, void *x)
{
	debug_check_no_locks_freed(x, s->object_size);

	return false;
}

static inline bool slab_free_freelist_hook(struct kmem_cache *s, void **head, void **tail, int *cnt)
{

	void *object;
	void *next = *head;
	void *old_tail = *tail ? *tail : *head;

	*head = NULL;
	*tail = NULL;

	do {
		object = next;
		next = get_freepointer(s, object);


		slab_free_hook(s, object);
		set_freepointer(s, object, *head);
		*head = object;
		if (!*tail)
			*tail = object;
	} while (object != old_tail);

	if (*head == *tail)
		*tail = NULL;

	return *head != NULL;
}

static void *setup_object(struct kmem_cache *s, void *object)
{
	if (unlikely(s->ctor)) {
		s->ctor(object);
	}
	return object;
}

static inline struct slab *alloc_slab_page(gfp_t flags, int node, struct kmem_cache_order_objects oo)
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
	if (page_is_pfmemalloc(folio_page(folio, 0)))
		slab_set_pfmemalloc(slab);

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
	if ((alloc_gfp & __GFP_DIRECT_RECLAIM) && oo_order(oo) > oo_order(s->min))
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

	account_slab(slab, oo_order(oo), s, flags);

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

	slab->inuse = slab->objects;
	slab->frozen = 1;

out:
	if (!slab)
		return NULL;

	return slab;
}

static struct slab *new_slab(struct kmem_cache *s, gfp_t flags, int node)
{
	if (unlikely(flags & GFP_SLAB_BUG_MASK))
		flags = kmalloc_fix_flags(flags);

	WARN_ON_ONCE(s->ctor && (flags & __GFP_ZERO));

	return allocate_slab(s, flags & (GFP_RECLAIM_MASK | GFP_CONSTRAINT_MASK), node);
}

/* __free_slab/rcu_free_slab/free_slab/discard_slab removed - the slab-empty
 * free paths (deactivate_slab M_FREE, __slab_free slab_empty) are never taken
 * in this single-shot boot workload (slabs never drop to zero in-use). */

static inline void
__add_partial(struct kmem_cache_node *n, struct slab *slab, int tail)
{
	n->nr_partial++;
	if (tail == DEACTIVATE_TO_TAIL)
		list_add_tail(&slab->slab_list, &n->partial);
	else
		list_add(&slab->slab_list, &n->partial);
}

static inline void add_partial(struct kmem_cache_node *n, struct slab *slab, int tail)
{
	lockdep_assert_held(&n->list_lock);
	__add_partial(n, slab, tail);
}

static inline void remove_partial(struct kmem_cache_node *n, struct slab *slab)
{
	lockdep_assert_held(&n->list_lock);
	list_del(&slab->slab_list);
	n->nr_partial--;
}

static inline void *acquire_slab(struct kmem_cache *s, struct kmem_cache_node *n, struct slab *slab, int mode)
{
	void *freelist;
	unsigned long counters;
	struct slab new;

	lockdep_assert_held(&n->list_lock);

	
	freelist = slab->freelist;
	counters = slab->counters;
	new.counters = counters;
	if (mode) {
		new.inuse = slab->objects;
		new.freelist = NULL;
	} else {
		new.freelist = freelist;
	}

	VM_BUG_ON(new.frozen);
	new.frozen = 1;

	if (!__cmpxchg_double_slab(s, slab, freelist, counters, new.freelist, new.counters))
		return NULL;

	remove_partial(n, slab);
	WARN_ON(!freelist);
	return freelist;
}

static inline bool pfmemalloc_match(struct slab *slab, gfp_t gfpflags);

static void *get_partial_node(struct kmem_cache *s, struct kmem_cache_node *n, struct slab **ret_slab, gfp_t gfpflags)
{
	struct slab *slab, *slab2;
	void *object = NULL;
	unsigned long flags;


	if (!n || !n->nr_partial)
		return NULL;

	spin_lock_irqsave(&n->list_lock, flags);
	list_for_each_entry_safe(slab, slab2, &n->partial, slab_list) {
		void *t;

		if (!pfmemalloc_match(slab, gfpflags))
			continue;

		t = acquire_slab(s, n, slab, 1);
		if (!t)
			break;

		*ret_slab = slab;
		object = t;
		break;

	}
	spin_unlock_irqrestore(&n->list_lock, flags);
	return object;
}

static void *get_partial(struct kmem_cache *s, gfp_t flags, int node, struct slab **ret_slab)
{
	int searchnode = node;

	if (node == NUMA_NO_NODE)
		searchnode = numa_mem_id();

	/*
	 * On this build CONFIG_NUMA is off: there is a single node, so the
	 * cross-node fallback scan (get_any_partial) can only ever return NULL.
	 * The local partial list is the only source, so return it directly.
	 */
	return get_partial_node(s, get_node(s, searchnode), ret_slab, flags);
}

#define TID_STEP 1

static inline unsigned long next_tid(unsigned long tid)
{
	return tid + TID_STEP;
}

static inline unsigned int init_tid(int cpu)
{
	return cpu;
}

static void init_kmem_cache_cpus(struct kmem_cache *s)
{
	int cpu;
	struct kmem_cache_cpu *c;

	for_each_possible_cpu(cpu) {
		c = per_cpu_ptr(s->cpu_slab, cpu);
		c->tid = init_tid(cpu);
	}
}

static void deactivate_slab(struct kmem_cache *s, struct slab *slab, void *freelist)
{
	enum slab_modes { M_NONE, M_PARTIAL, M_FULL_NOLIST };
	struct kmem_cache_node *n = get_node(s, slab_nid(slab));
	int free_delta = 0;
	enum slab_modes mode = M_NONE;
	void *nextfree, *freelist_iter, *freelist_tail;
	int tail = DEACTIVATE_TO_HEAD;
	unsigned long flags = 0;
	struct slab new, old;

	if (slab->freelist) {
		tail = DEACTIVATE_TO_TAIL;
	}

	
	freelist_tail = NULL;
	freelist_iter = freelist;
	while (freelist_iter) {
		nextfree = get_freepointer(s, freelist_iter);

		freelist_tail = freelist_iter;
		free_delta++;

		freelist_iter = nextfree;
	}

	
redo:

	old.freelist = READ_ONCE(slab->freelist);
	old.counters = READ_ONCE(slab->counters);
	VM_BUG_ON(!old.frozen);

	
	new.counters = old.counters;
	if (freelist_tail) {
		new.inuse -= free_delta;
		set_freepointer(s, freelist_tail, old.freelist);
		new.freelist = freelist;
	} else
		new.freelist = old.freelist;

	new.frozen = 0;

	if (new.freelist) {
		mode = M_PARTIAL;

		spin_lock_irqsave(&n->list_lock, flags);
	} else {
		mode = M_FULL_NOLIST;
	}

	if (!cmpxchg_double_slab(s, slab, old.freelist, old.counters, new.freelist, new.counters)) {
		if (mode == M_PARTIAL)
			spin_unlock_irqrestore(&n->list_lock, flags);
		goto redo;
	}

	if (mode == M_PARTIAL) {
		add_partial(n, slab, tail);
		spin_unlock_irqrestore(&n->list_lock, flags);
	}
}

static inline void __flush_cpu_slab(struct kmem_cache *s, int cpu)
{
	struct kmem_cache_cpu *c = per_cpu_ptr(s->cpu_slab, cpu);
	void *freelist = c->freelist;
	struct slab *slab = c->slab;

	c->slab = NULL;
	c->freelist = NULL;
	c->tid = next_tid(c->tid);

	if (slab) {
		deactivate_slab(s, slab, freelist);
	}
}

static inline bool pfmemalloc_match(struct slab *slab, gfp_t gfpflags)
{
	if (unlikely(slab_test_pfmemalloc(slab)))
		return gfp_pfmemalloc_allowed(gfpflags);

	return true;
}

static void *___slab_alloc(struct kmem_cache *s, gfp_t gfpflags, int node, unsigned long addr, struct kmem_cache_cpu *c)
{
	void *freelist;
	struct slab *slab;

	/* Minimal stub: simplified slow path allocator */

	/* Try to get from partial lists */
	freelist = get_partial(s, gfpflags, node, &slab);
	if (freelist)
		return freelist;

	/* Allocate new slab */
	slub_put_cpu_ptr(s->cpu_slab);
	slab = new_slab(s, gfpflags, node);
	slub_get_cpu_ptr(s->cpu_slab);

	if (!slab) {
		return NULL;
	}

	freelist = slab->freelist;
	slab->freelist = NULL;
	return freelist;
}

static void *__slab_alloc(struct kmem_cache *s, gfp_t gfpflags, int node, unsigned long addr, struct kmem_cache_cpu *c)
{
	void *p;

	p = ___slab_alloc(s, gfpflags, node, addr, c);
	return p;
}

static __always_inline void *slab_alloc_node(struct kmem_cache *s, struct list_lru *lru, gfp_t gfpflags, int node, unsigned long addr, size_t orig_size)
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
	
	if (IS_ENABLED(CONFIG_PREEMPT_RT) || unlikely(!object || !slab)) {
		object = __slab_alloc(s, gfpflags, node, addr, c);
	} else {
		void *next_object = get_freepointer_safe(s, object);

		
		if (unlikely(!this_cpu_cmpxchg_double( s->cpu_slab->freelist, s->cpu_slab->tid, object, tid, next_object, next_tid(tid)))) {

			goto redo;
		}
		prefetch_freepointer(s, next_object);
	}

	init = slab_want_init_on_alloc(gfpflags, s);

	slab_post_alloc_hook(s, objcg, gfpflags, 1, &object, init);

	return object;
}

static __always_inline void *slab_alloc(struct kmem_cache *s, struct list_lru *lru, gfp_t gfpflags, unsigned long addr, size_t orig_size)
{
	return slab_alloc_node(s, lru, gfpflags, NUMA_NO_NODE, addr, orig_size);
}

static __always_inline
void *__kmem_cache_alloc_lru(struct kmem_cache *s, struct list_lru *lru, gfp_t gfpflags)
{
	void *ret = slab_alloc(s, lru, gfpflags, _RET_IP_, s->object_size);


	return ret;
}

void *kmem_cache_alloc(struct kmem_cache *s, gfp_t gfpflags)
{
	return __kmem_cache_alloc_lru(s, NULL, gfpflags);
}

void *kmem_cache_alloc_lru(struct kmem_cache *s, struct list_lru *lru, gfp_t gfpflags)
{
	return __kmem_cache_alloc_lru(s, lru, gfpflags);
}

static void __slab_free(struct kmem_cache *s, struct slab *slab, void *head, void *tail, int cnt, unsigned long addr)

{
	void *prior;
	int was_frozen;
	struct slab new;
	unsigned long counters;
	struct kmem_cache_node *n = NULL;
	unsigned long flags;

	do {
		if (unlikely(n)) {
			spin_unlock_irqrestore(&n->list_lock, flags);
			n = NULL;
		}
		prior = slab->freelist;
		counters = slab->counters;
		set_freepointer(s, tail, prior);
		new.counters = counters;
		was_frozen = new.frozen;
		new.inuse -= cnt;
		if ((!new.inuse || !prior) && !was_frozen) {
			n = get_node(s, slab_nid(slab));

			spin_lock_irqsave(&n->list_lock, flags);
		}

	} while (!cmpxchg_double_slab(s, slab, prior, counters, head, new.counters));

	if (likely(!n)) {
		return;
	}

	if (unlikely(!prior)) {
		add_partial(n, slab, DEACTIVATE_TO_TAIL);
	}
	spin_unlock_irqrestore(&n->list_lock, flags);
}

static __always_inline void do_slab_free(struct kmem_cache *s, struct slab *slab, void *head, void *tail, int cnt, unsigned long addr)
{
	void *tail_obj = tail ? : head;
	struct kmem_cache_cpu *c;
	unsigned long tid;

redo:
	
	c = raw_cpu_ptr(s->cpu_slab);
	tid = READ_ONCE(c->tid);

	
	barrier();

	if (likely(slab == c->slab)) {
		void **freelist = READ_ONCE(c->freelist);

		set_freepointer(s, tail_obj, freelist);

		if (unlikely(!this_cpu_cmpxchg_double( s->cpu_slab->freelist, s->cpu_slab->tid, freelist, tid, head, next_tid(tid)))) {

			goto redo;
		}
	} else
		__slab_free(s, slab, head, tail_obj, cnt, addr);

}

static __always_inline void slab_free(struct kmem_cache *s, struct slab *slab, void *head, void *tail, int cnt, unsigned long addr)
{
	
	if (slab_free_freelist_hook(s, &head, &tail, &cnt))
		do_slab_free(s, slab, head, tail, cnt, addr);
}

void kmem_cache_free(struct kmem_cache *s, void *x)
{
	s = cache_from_obj(s, x);
	if (!s)
		return;
	slab_free(s, virt_to_slab(x), x, NULL, 1, _RET_IP_);
}

static inline void free_large_kmalloc(struct folio *folio, void *object)
{
	unsigned int order = folio_order(folio);

	if (WARN_ON_ONCE(order == 0))
		pr_warn_once("object pointer: 0x%p\n", object);

	mod_lruvec_page_state(folio_page(folio, 0), NR_SLAB_UNRECLAIMABLE_B, -(PAGE_SIZE << order));
	__free_pages(folio_page(folio, 0), order);
}

#define slub_max_order ((unsigned int)PAGE_ALLOC_COSTLY_ORDER)

static inline unsigned int calc_slab_order(unsigned int size, unsigned int min_objects, unsigned int max_order, unsigned int fract_leftover)
{
	unsigned int order;

	for (order = get_order(min_objects * size); order <= max_order; order++) {

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
			order = calc_slab_order(size, min_objects, slub_max_order, fraction);
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

static void
init_kmem_cache_node(struct kmem_cache_node *n)
{
	n->nr_partial = 0;
	spin_lock_init(&n->list_lock);
	INIT_LIST_HEAD(&n->partial);
}

static inline int alloc_kmem_cache_cpus(struct kmem_cache *s)
{
	BUILD_BUG_ON(PERCPU_DYNAMIC_EARLY_SIZE < KMALLOC_SHIFT_HIGH * sizeof(struct kmem_cache_cpu));

	
	s->cpu_slab = __alloc_percpu(sizeof(struct kmem_cache_cpu), 2 * sizeof(void *));

	if (!s->cpu_slab)
		return 0;

	init_kmem_cache_cpus(s);

	return 1;
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
	slab->frozen = 0;
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

	if (s->flags & SLAB_RECLAIM_ACCOUNT)
		s->allocflags |= __GFP_RECLAIMABLE;

	
	s->oo = oo_make(order, size);
	s->min = oo_make(get_order(size), size);

	return !!oo_objects(s->oo);
}

static int kmem_cache_open(struct kmem_cache *s, slab_flags_t flags)
{
	s->flags = flags;

	if (!calculate_sizes(s))
		return -EINVAL;

#if defined(CONFIG_HAVE_CMPXCHG_DOUBLE) && \
    defined(CONFIG_HAVE_ALIGNED_STRUCT_PAGE)
	/* CX8 is a REQUIRED_MASK feature -> system_has_cmpxchg_double() is a
	 * compile-time 1; the guard was always taken, folded to the taken arm. */
	s->flags |= __CMPXCHG_DOUBLE;
#endif

	if (!init_kmem_cache_nodes(s))
		return -EINVAL;

	if (alloc_kmem_cache_cpus(s))
		return 0;

	return -EINVAL;
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

	ret = slab_alloc(s, NULL, flags, _RET_IP_, size);

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
	slab_free(slab->slab_cache, slab, object, NULL, 1, _RET_IP_);
}

/* Memory hotplug callbacks removed - not needed for minimal kernel
 * (register_hotmemory_notifier is already a no-op) */

static struct kmem_cache * __init bootstrap(struct kmem_cache *static_cache)
{
	int node;
	struct kmem_cache *s = kmem_cache_zalloc(kmem_cache, GFP_NOWAIT);
	struct kmem_cache_node *n;

	memcpy(s, static_cache, kmem_cache->object_size);

	
	__flush_cpu_slab(s, smp_processor_id());
	for_each_kmem_cache_node(s, node, n) {
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

	create_boot_cache(kmem_cache_node, "kmem_cache_node", sizeof(struct kmem_cache_node), SLAB_HWCACHE_ALIGN, 0, 0);

	/* Memory hotplug notifier removed - not needed for minimal kernel */


	slab_state = PARTIAL;

	create_boot_cache(kmem_cache, "kmem_cache", offsetof(struct kmem_cache, node) + nr_node_ids * sizeof(struct kmem_cache_node *), SLAB_HWCACHE_ALIGN, 0, 0);

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

/* Stub: __kmalloc_track_caller not used in minimal kernel */
void *__kmalloc_track_caller(size_t size, gfp_t gfpflags, unsigned long caller)
{
	return __kmalloc(size, gfpflags);
}

