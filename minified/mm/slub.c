
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/module.h>
#include <linux/bit_spinlock.h>
#include <linux/interrupt.h>
#include <linux/swab.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include "slab.h"
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kasan.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/mempolicy.h>
#include <linux/ctype.h>
#include <linux/stackdepot.h>
#include <linux/kallsyms.h>
#include <linux/kfence.h>
#include <linux/memory.h>
#include <linux/math64.h>
#include <linux/stacktrace.h>

#include <linux/memcontrol.h>
#include <linux/random.h>
#include <linux/sort.h>

#include <linux/debugfs.h>

#include "internal.h"

#define slub_get_cpu_ptr(var) get_cpu_ptr(var)
#define slub_put_cpu_ptr(var) put_cpu_ptr(var)

static inline bool kmem_cache_debug(struct kmem_cache *s)
{
	return kmem_cache_debug_flags(s, SLAB_DEBUG_FLAGS);
}

void *fixup_red_left(struct kmem_cache *s, void *p)
{
	if (kmem_cache_debug_flags(s, SLAB_RED_ZONE))
		p += s->red_left_pad;

	return p;
}

#define MIN_PARTIAL 5

#define MAX_PARTIAL 10

#define DEBUG_DEFAULT_FLAGS                                      \
	(SLAB_CONSISTENCY_CHECKS | SLAB_RED_ZONE | SLAB_POISON | \
	 SLAB_STORE_USER)

#define SLAB_NO_CMPXCHG (SLAB_CONSISTENCY_CHECKS | SLAB_STORE_USER | SLAB_TRACE)

#define DEBUG_METADATA_FLAGS (SLAB_RED_ZONE | SLAB_POISON | SLAB_STORE_USER)

#define OO_SHIFT 16
#define OO_MASK ((1 << OO_SHIFT) - 1)
#define MAX_OBJS_PER_PAGE 32767

#define __OBJECT_POISON ((slab_flags_t __force)0x80000000U)

#define __CMPXCHG_DOUBLE ((slab_flags_t __force)0x40000000U)

#define TRACK_ADDRS_COUNT 16
struct track {
	unsigned long addr;
	int cpu;
	int pid;
	unsigned long when;
};

enum track_item { TRACK_ALLOC, TRACK_FREE };

static inline int sysfs_slab_add(struct kmem_cache *s)
{
	return 0;
}
static inline int sysfs_slab_alias(struct kmem_cache *s, const char *p)
{
	return 0;
}

static inline void debugfs_slab_add(struct kmem_cache *s)
{
}

static inline void stat(const struct kmem_cache *s, enum stat_item si)
{
}

static nodemask_t slab_nodes;

static inline void *freelist_ptr(const struct kmem_cache *s, void *ptr,
				 unsigned long ptr_addr)
{
	return ptr;
}

static inline void *freelist_dereference(const struct kmem_cache *s,
					 void *ptr_addr)
{
	return freelist_ptr(s, (void *)*(unsigned long *)(ptr_addr),
			    (unsigned long)ptr_addr);
}

static inline void *get_freepointer(struct kmem_cache *s, void *object)
{
	object = kasan_reset_tag(object);
	return freelist_dereference(s, object + s->offset);
}

static void prefetch_freepointer(const struct kmem_cache *s, void *object)
{
	prefetchw(object + s->offset);
}

static inline void *get_freepointer_safe(struct kmem_cache *s, void *object)
{
	unsigned long freepointer_addr;
	void *p;

	if (!debug_pagealloc_enabled_static())
		return get_freepointer(s, object);

	object = kasan_reset_tag(object);
	freepointer_addr = (unsigned long)object + s->offset;
	copy_from_kernel_nofault(&p, (void **)freepointer_addr, sizeof(p));
	return freelist_ptr(s, p, freepointer_addr);
}

static inline void set_freepointer(struct kmem_cache *s, void *object, void *fp)
{
	unsigned long freeptr_addr = (unsigned long)object + s->offset;

	freeptr_addr = (unsigned long)kasan_reset_tag((void *)freeptr_addr);
	*(void **)freeptr_addr = freelist_ptr(s, fp, freeptr_addr);
}

#define for_each_object(__p, __s, __addr, __objects) \
	for (__p = fixup_red_left(__s, __addr);      \
	     __p < (__addr) + (__objects) * (__s)->size; __p += (__s)->size)

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

static __always_inline void slab_lock(struct slab *slab, unsigned long *flags)
{
	/* CONFIG_PREEMPT_RT not enabled - no irq save */
	__slab_lock(slab);
}

static __always_inline void slab_unlock(struct slab *slab, unsigned long *flags)
{
	__slab_unlock(slab);
	/* CONFIG_PREEMPT_RT not enabled - no irq restore */
}

static inline bool __cmpxchg_double_slab(struct kmem_cache *s,
					 struct slab *slab, void *freelist_old,
					 unsigned long counters_old,
					 void *freelist_new,
					 unsigned long counters_new,
					 const char *n)
{
	/* CONFIG_PREEMPT_RT not enabled */
	lockdep_assert_irqs_disabled();
#if defined(CONFIG_HAVE_CMPXCHG_DOUBLE) && \
	defined(CONFIG_HAVE_ALIGNED_STRUCT_PAGE)
	if (s->flags & __CMPXCHG_DOUBLE) {
		if (cmpxchg_double(&slab->freelist, &slab->counters,
				   freelist_old, counters_old, freelist_new,
				   counters_new))
			return true;
	} else
#endif
	{

		unsigned long flags = 0;

		slab_lock(slab, &flags);
		if (slab->freelist == freelist_old &&
		    slab->counters == counters_old) {
			slab->freelist = freelist_new;
			slab->counters = counters_new;
			slab_unlock(slab, &flags);
			return true;
		}
		slab_unlock(slab, &flags);
	}

	cpu_relax();
	stat(s, CMPXCHG_DOUBLE_FAIL);
	return false;
}

static inline bool
cmpxchg_double_slab(struct kmem_cache *s, struct slab *slab, void *freelist_old,
		    unsigned long counters_old, void *freelist_new,
		    unsigned long counters_new, const char *n)
{
#if defined(CONFIG_HAVE_CMPXCHG_DOUBLE) && \
	defined(CONFIG_HAVE_ALIGNED_STRUCT_PAGE)
	if (s->flags & __CMPXCHG_DOUBLE) {
		if (cmpxchg_double(&slab->freelist, &slab->counters,
				   freelist_old, counters_old, freelist_new,
				   counters_new))
			return true;
	} else
#endif
	{
		unsigned long flags;

		local_irq_save(flags);
		__slab_lock(slab);
		if (slab->freelist == freelist_old &&
		    slab->counters == counters_old) {
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
	stat(s, CMPXCHG_DOUBLE_FAIL);
	return false;
}

static inline void setup_object_debug(struct kmem_cache *s, void *object)
{
}
static inline void setup_slab_debug(struct kmem_cache *s, struct slab *slab,
				    void *addr)
{
}

static inline int free_debug_processing(struct kmem_cache *s, struct slab *slab,
					void *head, void *tail, int bulk_cnt,
					unsigned long addr)
{
	return 0;
}

static inline void slab_pad_check(struct kmem_cache *s, struct slab *slab)
{
}
static inline int check_object(struct kmem_cache *s, struct slab *slab,
			       void *object, u8 val)
{
	return 1;
}
static inline void add_full(struct kmem_cache *s, struct kmem_cache_node *n,
			    struct slab *slab)
{
}
static inline void remove_full(struct kmem_cache *s, struct kmem_cache_node *n,
			       struct slab *slab)
{
}
slab_flags_t kmem_cache_flags(unsigned int object_size, slab_flags_t flags,
			      const char *name)
{
	return flags;
}
#define slub_debug 0

#define disable_higher_order_debug 0

static inline unsigned long slabs_node(struct kmem_cache *s, int node)
{
	return 0;
}
static inline void inc_slabs_node(struct kmem_cache *s, int node, int objects)
{
}
static inline void dec_slabs_node(struct kmem_cache *s, int node, int objects)
{
}

/* Removed: kfree_hook, slab_free_hook, slab_free_freelist_hook
 * - No longer needed since kfree/kmem_cache_free are no-ops */

static void *setup_object(struct kmem_cache *s, void *object)
{
	setup_object_debug(s, object);
	object = kasan_init_slab_obj(s, object);
	if (unlikely(s->ctor)) {
		kasan_unpoison_object_data(s, object);
		s->ctor(object);
		kasan_poison_object_data(s, object);
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
	if (page_is_pfmemalloc(folio_page(folio, 0)))
		slab_set_pfmemalloc(slab);

	return slab;
}

static inline int init_cache_random_seq(struct kmem_cache *s)
{
	return 0;
}
static inline void init_freelist_randomization(void)
{
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
		stat(s, ORDER_FALLBACK);
	}

	slab->objects = oo_objects(oo);

	account_slab(slab, oo_order(oo), s, flags);

	slab->slab_cache = s;

	kasan_poison_slab(slab);

	start = slab_address(slab);

	setup_slab_debug(s, slab, start);

	start = fixup_red_left(s, start);
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

out:
	if (!slab)
		return NULL;

	inc_slabs_node(s, slab_nid(slab), slab->objects);

	return slab;
}

static struct slab *new_slab(struct kmem_cache *s, gfp_t flags, int node)
{
	if (unlikely(flags & GFP_SLAB_BUG_MASK))
		flags = kmalloc_fix_flags(flags);

	WARN_ON_ONCE(s->ctor && (flags & __GFP_ZERO));

	return allocate_slab(
		s, flags & (GFP_RECLAIM_MASK | GFP_CONSTRAINT_MASK), node);
}

/* Removed: __free_slab, rcu_free_slab, free_slab
 * - Simplified since __free_pages is a no-op (~35 LOC) */

static void discard_slab(struct kmem_cache *s, struct slab *slab)
{
	/* No-op: bump allocator style - no deallocation */
	dec_slabs_node(s, slab_nid(slab), slab->objects);
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

static inline void add_partial(struct kmem_cache_node *n, struct slab *slab,
			       int tail)
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

static inline void *acquire_slab(struct kmem_cache *s,
				 struct kmem_cache_node *n, struct slab *slab,
				 int mode)
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

	if (!__cmpxchg_double_slab(s, slab, freelist, counters, new.freelist,
				   new.counters, "acquire_slab"))
		return NULL;

	remove_partial(n, slab);
	WARN_ON(!freelist);
	return freelist;
}

static inline void put_cpu_partial(struct kmem_cache *s, struct slab *slab,
				   int drain)
{
}
static inline bool pfmemalloc_match(struct slab *slab, gfp_t gfpflags);

static void *get_partial_node(struct kmem_cache *s, struct kmem_cache_node *n,
			      struct slab **ret_slab, gfp_t gfpflags)
{
	struct slab *slab, *slab2;
	void *object = NULL;
	unsigned long flags;
	unsigned int partial_slabs = 0;

	if (!n || !n->nr_partial)
		return NULL;

	spin_lock_irqsave(&n->list_lock, flags);
	list_for_each_entry_safe(slab, slab2, &n->partial, slab_list) {
		void *t;

		if (!pfmemalloc_match(slab, gfpflags))
			continue;

		t = acquire_slab(s, n, slab, object == NULL);
		if (!t)
			break;

		if (!object) {
			*ret_slab = slab;
			stat(s, ALLOC_FROM_PARTIAL);
			object = t;
		} else {
			put_cpu_partial(s, slab, 0);
			stat(s, CPU_PARTIAL_NODE);
			partial_slabs++;
		}
		break;
	}
	spin_unlock_irqrestore(&n->list_lock, flags);
	return object;
}

static void *get_any_partial(struct kmem_cache *s, gfp_t flags,
			     struct slab **ret_slab)
{
	return NULL;
}

static void *get_partial(struct kmem_cache *s, gfp_t flags, int node,
			 struct slab **ret_slab)
{
	void *object;
	int searchnode = node;

	if (node == NUMA_NO_NODE)
		searchnode = numa_mem_id();

	object = get_partial_node(s, get_node(s, searchnode), ret_slab, flags);
	if (object || node != NUMA_NO_NODE)
		return object;

	return get_any_partial(s, flags, ret_slab);
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

static inline void note_cmpxchg_failure(const char *n,
					const struct kmem_cache *s,
					unsigned long tid)
{
	stat(s, CMPXCHG_DOUBLE_CPU_FAIL);
}

static void init_kmem_cache_cpus(struct kmem_cache *s)
{
	int cpu;
	struct kmem_cache_cpu *c;

	for_each_possible_cpu(cpu) {
		c = per_cpu_ptr(s->cpu_slab, cpu);
		local_lock_init(&c->lock);
		c->tid = init_tid(cpu);
	}
}

/* Removed: deactivate_slab - dead code since flush_slab simplified (~80 LOC) */

static inline void unfreeze_partials(struct kmem_cache *s)
{
}
static inline void unfreeze_partials_cpu(struct kmem_cache *s,
					 struct kmem_cache_cpu *c)
{
}

static inline void flush_slab(struct kmem_cache *s, struct kmem_cache_cpu *c)
{
	/* Simplified: just clear the cpu slab */
	unsigned long flags;
	local_lock_irqsave(&s->cpu_slab->lock, flags);
	c->slab = NULL;
	c->freelist = NULL;
	c->tid = next_tid(c->tid);
	local_unlock_irqrestore(&s->cpu_slab->lock, flags);
}

static inline void __flush_cpu_slab(struct kmem_cache *s, int cpu)
{
	/* Simplified: just clear the cpu slab without deactivation */
	struct kmem_cache_cpu *c = per_cpu_ptr(s->cpu_slab, cpu);
	c->slab = NULL;
	c->freelist = NULL;
	c->tid = next_tid(c->tid);
}

struct slub_flush_work {
	struct work_struct work;
	struct kmem_cache *s;
	bool skip;
};

static void flush_cpu_slab(struct work_struct *w)
{
	struct kmem_cache *s;
	struct kmem_cache_cpu *c;
	struct slub_flush_work *sfw;

	sfw = container_of(w, struct slub_flush_work, work);

	s = sfw->s;
	c = this_cpu_ptr(s->cpu_slab);

	if (c->slab)
		flush_slab(s, c);

	unfreeze_partials(s);
}

static bool has_cpu_slab(int cpu, struct kmem_cache *s)
{
	struct kmem_cache_cpu *c = per_cpu_ptr(s->cpu_slab, cpu);

	return c->slab || slub_percpu_partial(c);
}

static DEFINE_MUTEX(flush_lock);
static DEFINE_PER_CPU(struct slub_flush_work, slub_flush);

static void flush_all_cpus_locked(struct kmem_cache *s)
{
	struct slub_flush_work *sfw;
	unsigned int cpu;

	lockdep_assert_cpus_held();
	mutex_lock(&flush_lock);

	for_each_online_cpu(cpu) {
		sfw = &per_cpu(slub_flush, cpu);
		if (!has_cpu_slab(cpu, s)) {
			sfw->skip = true;
			continue;
		}
		INIT_WORK(&sfw->work, flush_cpu_slab);
		sfw->skip = false;
		sfw->s = s;
		schedule_work_on(cpu, &sfw->work);
	}

	for_each_online_cpu(cpu) {
		sfw = &per_cpu(slub_flush, cpu);
		if (sfw->skip)
			continue;
		flush_work(&sfw->work);
	}

	mutex_unlock(&flush_lock);
}

static int slub_cpu_dead(unsigned int cpu)
{
	struct kmem_cache *s;

	mutex_lock(&slab_mutex);
	list_for_each_entry(s, &slab_caches, list)
		__flush_cpu_slab(s, cpu);
	mutex_unlock(&slab_mutex);
	return 0;
}

static inline int node_match(struct slab *slab, int node)
{
	return 1;
}

static noinline void slab_out_of_memory(struct kmem_cache *s, gfp_t gfpflags,
					int nid)
{
}

static inline bool pfmemalloc_match(struct slab *slab, gfp_t gfpflags)
{
	if (unlikely(slab_test_pfmemalloc(slab)))
		return gfp_pfmemalloc_allowed(gfpflags);

	return true;
}

static inline void *get_freelist(struct kmem_cache *s, struct slab *slab)
{
	struct slab new;
	unsigned long counters;
	void *freelist;

	lockdep_assert_held(this_cpu_ptr(&s->cpu_slab->lock));

	do {
		freelist = slab->freelist;
		counters = slab->counters;

		new.counters = counters;
		VM_BUG_ON(!new.frozen);

		new.inuse = slab->objects;
		new.frozen = freelist != NULL;

	} while (!__cmpxchg_double_slab(s, slab, freelist, counters, NULL,
					new.counters, "get_freelist"));

	return freelist;
}

static void *___slab_alloc(struct kmem_cache *s, gfp_t gfpflags, int node,
			   unsigned long addr, struct kmem_cache_cpu *c)
{
	void *freelist;
	struct slab *slab;

	/* Minimal stub: simplified slow path allocator */
	(void)addr;
	(void)c;

	/* Try to get from partial lists */
	freelist = get_partial(s, gfpflags, node, &slab);
	if (freelist)
		return freelist;

	/* Allocate new slab */
	slub_put_cpu_ptr(s->cpu_slab);
	slab = new_slab(s, gfpflags, node);
	slub_get_cpu_ptr(s->cpu_slab);

	if (!slab) {
		slab_out_of_memory(s, gfpflags, node);
		return NULL;
	}

	freelist = slab->freelist;
	slab->freelist = NULL;
	return freelist;
}

static void *__slab_alloc(struct kmem_cache *s, gfp_t gfpflags, int node,
			  unsigned long addr, struct kmem_cache_cpu *c)
{
	void *p;

	p = ___slab_alloc(s, gfpflags, node, addr, c);
	return p;
}

static __always_inline void maybe_wipe_obj_freeptr(struct kmem_cache *s,
						   void *obj)
{
	if (unlikely(slab_want_init_on_free(s)) && obj)
		memset((void *)((char *)kasan_reset_tag(obj) + s->offset), 0,
		       sizeof(void *));
}

static __always_inline void *
slab_alloc_node(struct kmem_cache *s, struct list_lru *lru, gfp_t gfpflags,
		int node, unsigned long addr, size_t orig_size)
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

	object = kfence_alloc(s, orig_size, gfpflags);
	if (unlikely(object))
		goto out;

redo:

	c = raw_cpu_ptr(s->cpu_slab);
	tid = READ_ONCE(c->tid);

	barrier();

	object = c->freelist;
	slab = c->slab;
	/* CONFIG_PREEMPT_RT not enabled */
	if (unlikely(!object || !slab || !node_match(slab, node))) {
		object = __slab_alloc(s, gfpflags, node, addr, c);
	} else {
		void *next_object = get_freepointer_safe(s, object);

		if (unlikely(!this_cpu_cmpxchg_double(
			    s->cpu_slab->freelist, s->cpu_slab->tid, object,
			    tid, next_object, next_tid(tid)))) {
			note_cmpxchg_failure("slab_alloc", s, tid);
			goto redo;
		}
		prefetch_freepointer(s, next_object);
		stat(s, ALLOC_FASTPATH);
	}

	maybe_wipe_obj_freeptr(s, object);
	init = slab_want_init_on_alloc(gfpflags, s);

out:
	slab_post_alloc_hook(s, objcg, gfpflags, 1, &object, init);

	return object;
}

static __always_inline void *slab_alloc(struct kmem_cache *s,
					struct list_lru *lru, gfp_t gfpflags,
					unsigned long addr, size_t orig_size)
{
	return slab_alloc_node(s, lru, gfpflags, NUMA_NO_NODE, addr, orig_size);
}

static __always_inline void *__kmem_cache_alloc_lru(struct kmem_cache *s,
						    struct list_lru *lru,
						    gfp_t gfpflags)
{
	void *ret = slab_alloc(s, lru, gfpflags, _RET_IP_, s->object_size);

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

/* Removed: __slab_free, do_slab_free, slab_free, free_large_kmalloc
 * - All dead code since kfree/kmem_cache_free are no-ops (~115 lines) */

void kmem_cache_free(struct kmem_cache *s, void *x)
{
	/* No-op: bump allocator style - no deallocation */
}

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
	unsigned int nr_cpus;

	min_objects = slub_min_objects;
	if (!min_objects) {
		nr_cpus = num_present_cpus();
		if (nr_cpus <= 1)
			nr_cpus = nr_cpu_ids;
		min_objects = 4 * (fls(nr_cpus) + 1);
	}
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

static inline int alloc_kmem_cache_cpus(struct kmem_cache *s)
{
	BUILD_BUG_ON(PERCPU_DYNAMIC_EARLY_SIZE <
		     KMALLOC_SHIFT_HIGH * sizeof(struct kmem_cache_cpu));

	s->cpu_slab = __alloc_percpu(sizeof(struct kmem_cache_cpu),
				     2 * sizeof(void *));

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
	if (slab_nid(slab) != node) {
		pr_err("SLUB: Unable to allocate memory from node %d\n", node);
		pr_err("SLUB: Allocating a useless per node structure in order to be able to continue\n");
	}

	n = slab->freelist;
	BUG_ON(!n);
	n = kasan_slab_alloc(kmem_cache_node, n, GFP_KERNEL, false);
	slab->freelist = get_freepointer(kmem_cache_node, n);
	slab->inuse = 1;
	slab->frozen = 0;
	kmem_cache_node->node[node] = n;
	init_kmem_cache_node(n);
	inc_slabs_node(kmem_cache_node, node, slab->objects);

	__add_partial(n, slab, DEACTIVATE_TO_HEAD);
}

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

void __kmem_cache_release(struct kmem_cache *s)
{
	cache_random_seq_destroy(s);
	free_percpu(s->cpu_slab);
	free_kmem_cache_nodes(s);
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

		if (!n) {
			free_kmem_cache_nodes(s);
			return 0;
		}

		init_kmem_cache_node(n);
		s->node[node] = n;
	}
	return 1;
}

static void set_cpu_partial(struct kmem_cache *s)
{
}

static int calculate_sizes(struct kmem_cache *s)
{
	slab_flags_t flags = s->flags;
	unsigned int size = s->object_size;
	unsigned int order;

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

	kasan_cache_create(s, &size, &s->flags);

	size = ALIGN(size, s->align);
	s->size = size;
	s->reciprocal_size = reciprocal_value(size);
	order = calculate_order(size);

	if ((int)order < 0)
		return 0;

	s->allocflags = 0;
	if (order)
		s->allocflags |= __GFP_COMP;

	if (s->flags & SLAB_CACHE_DMA)
		s->allocflags |= GFP_DMA;

	if (s->flags & SLAB_CACHE_DMA32)
		s->allocflags |= GFP_DMA32;

	if (s->flags & SLAB_RECLAIM_ACCOUNT)
		s->allocflags |= __GFP_RECLAIMABLE;

	s->oo = oo_make(order, size);
	s->min = oo_make(get_order(size), size);

	return !!oo_objects(s->oo);
}

static int kmem_cache_open(struct kmem_cache *s, slab_flags_t flags)
{
	s->flags = kmem_cache_flags(s->size, flags, s->name);

	if (!calculate_sizes(s))
		goto error;
	if (disable_higher_order_debug) {
		if (get_order(s->size) > get_order(s->object_size)) {
			s->flags &= ~DEBUG_METADATA_FLAGS;
			s->offset = 0;
			if (!calculate_sizes(s))
				goto error;
		}
	}

#if defined(CONFIG_HAVE_CMPXCHG_DOUBLE) && \
	defined(CONFIG_HAVE_ALIGNED_STRUCT_PAGE)
	if (system_has_cmpxchg_double() && (s->flags & SLAB_NO_CMPXCHG) == 0)

		s->flags |= __CMPXCHG_DOUBLE;
#endif

	s->min_partial = min_t(unsigned long, MAX_PARTIAL, ilog2(s->size) / 2);
	s->min_partial = max_t(unsigned long, MIN_PARTIAL, s->min_partial);

	set_cpu_partial(s);

	if (slab_state >= UP) {
		if (init_cache_random_seq(s))
			goto error;
	}

	if (!init_kmem_cache_nodes(s))
		goto error;

	if (alloc_kmem_cache_cpus(s))
		return 0;

error:
	__kmem_cache_release(s);
	return -EINVAL;
}

static void list_slab_objects(struct kmem_cache *s, struct slab *slab,
			      const char *text)
{
}

static void free_partial(struct kmem_cache *s, struct kmem_cache_node *n)
{
	LIST_HEAD(discard);
	struct slab *slab, *h;

	BUG_ON(irqs_disabled());
	spin_lock_irq(&n->list_lock);
	list_for_each_entry_safe(slab, h, &n->partial, slab_list) {
		if (!slab->inuse) {
			remove_partial(n, slab);
			list_add(&slab->slab_list, &discard);
		} else {
			list_slab_objects(
				s, slab,
				"Objects remaining in %s on __kmem_cache_shutdown()");
		}
	}
	spin_unlock_irq(&n->list_lock);

	list_for_each_entry_safe(slab, h, &discard, slab_list)
		discard_slab(s, slab);
}

int __kmem_cache_shutdown(struct kmem_cache *s)
{
	int node;
	struct kmem_cache_node *n;

	flush_all_cpus_locked(s);

	for_each_kmem_cache_node(s, node, n)
	{
		free_partial(s, n);
		if (n->nr_partial || slabs_node(s, node))
			return 1;
	}
	return 0;
}

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

	ret = slab_alloc(s, NULL, flags, _RET_IP_, size);

	ret = kasan_kmalloc(s, ret, size, flags);

	return ret;
}

size_t __ksize(const void *object)
{
	struct folio *folio;

	if (unlikely(object == ZERO_SIZE_PTR))
		return 0;

	folio = virt_to_folio(object);

	if (unlikely(!folio_test_slab(folio)))
		return folio_size(folio);

	return slab_ksize(folio_slab(folio)->slab_cache);
}

void kfree(const void *x)
{
	/* No-op: bump allocator style - no deallocation */
}

/* Memory hotplug callbacks removed - not needed for minimal kernel
 * (register_hotmemory_notifier is already a no-op) */

static struct kmem_cache *__init bootstrap(struct kmem_cache *static_cache)
{
	int node;
	struct kmem_cache *s = kmem_cache_zalloc(kmem_cache, GFP_NOWAIT);
	struct kmem_cache_node *n;

	memcpy(s, static_cache, kmem_cache->object_size);

	__flush_cpu_slab(s, smp_processor_id());
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
	int node;

	if (debug_guardpage_minorder())
		slub_max_order = 0;

	if (__slub_debug_enabled())
		no_hash_pointers_enable(NULL);

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

	init_freelist_randomization();

	cpuhp_setup_state_nocalls(CPUHP_SLUB_DEAD, "slub:dead", NULL,
				  slub_cpu_dead);
}

void __init kmem_cache_init_late(void)
{
}

struct kmem_cache *__kmem_cache_alias(const char *name, unsigned int size,
				      unsigned int align, slab_flags_t flags,
				      void (*ctor)(void *))
{
	struct kmem_cache *s;

	s = find_mergeable(size, align, flags, name, ctor);
	if (s) {
		s->refcount++;

		s->object_size = max(s->object_size, size);
		s->inuse = max(s->inuse, ALIGN(size, sizeof(void *)));

		if (sysfs_slab_alias(s, name)) {
			s->refcount--;
			s = NULL;
		}
	}

	return s;
}

int __kmem_cache_create(struct kmem_cache *s, slab_flags_t flags)
{
	int err;

	err = kmem_cache_open(s, flags);
	if (err)
		return err;

	if (slab_state <= UP)
		return 0;

	err = sysfs_slab_add(s);
	if (err) {
		__kmem_cache_release(s);
		return err;
	}

	if (s->flags & SLAB_STORE_USER)
		debugfs_slab_add(s);

	return 0;
}

/* Stub: __kmalloc_track_caller not used in minimal kernel */
void *__kmalloc_track_caller(size_t size, gfp_t gfpflags, unsigned long caller)
{
	return __kmalloc(size, gfpflags);
}
