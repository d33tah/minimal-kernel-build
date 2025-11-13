// SPDX-License-Identifier: GPL-2.0-only

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/kthread.h>
#include <linux/hardirq.h>
#include <linux/mempolicy.h>
#include <linux/freezer.h>
#include <linux/debug_locks.h>
#include <linux/lockdep.h>
#include <linux/idr.h>
#include <linux/jhash.h>
#include <linux/hashtable.h>
#include <linux/rculist.h>
#include <linux/nodemask.h>
#include <linux/moduleparam.h>
#include <linux/uaccess.h>
#include <linux/sched/isolation.h>
#include <linux/nmi.h>

#include "workqueue_internal.h"

enum {
	
	POOL_MANAGER_ACTIVE	= 1 << 0,	
	POOL_DISASSOCIATED	= 1 << 2,	

	
	WORKER_DIE		= 1 << 1,	
	WORKER_IDLE		= 1 << 2,	
	WORKER_PREP		= 1 << 3,	
	WORKER_CPU_INTENSIVE	= 1 << 6,	
	WORKER_UNBOUND		= 1 << 7,	
	WORKER_REBOUND		= 1 << 8,	

	WORKER_NOT_RUNNING	= WORKER_PREP | WORKER_CPU_INTENSIVE |
				  WORKER_UNBOUND | WORKER_REBOUND,

	NR_STD_WORKER_POOLS	= 2,		

	UNBOUND_POOL_HASH_ORDER	= 6,		
	BUSY_WORKER_HASH_ORDER	= 6,		

	MAX_IDLE_WORKERS_RATIO	= 4,		
	IDLE_WORKER_TIMEOUT	= 300 * HZ,	

	MAYDAY_INITIAL_TIMEOUT  = HZ / 100 >= 2 ? HZ / 100 : 2,
						
	MAYDAY_INTERVAL		= HZ / 10,	
	CREATE_COOLDOWN		= HZ,		

	
	RESCUER_NICE_LEVEL	= MIN_NICE,
	HIGHPRI_NICE_LEVEL	= MIN_NICE,

	WQ_NAME_LEN		= 24,
};

struct worker_pool {
	raw_spinlock_t		lock;		
	int			cpu;		
	int			node;		
	int			id;		
	unsigned int		flags;		

	unsigned long		watchdog_ts;	

	
	int			nr_running;

	struct list_head	worklist;	

	int			nr_workers;	
	int			nr_idle;	

	struct list_head	idle_list;	
	struct timer_list	idle_timer;	
	struct timer_list	mayday_timer;	

	
	DECLARE_HASHTABLE(busy_hash, BUSY_WORKER_HASH_ORDER);
						

	struct worker		*manager;	
	struct list_head	workers;	
	struct completion	*detach_completion; 

	struct ida		worker_ida;	

	struct workqueue_attrs	*attrs;		
	struct hlist_node	hash_node;	
	int			refcnt;		

	
	struct rcu_head		rcu;
};

struct pool_workqueue {
	struct worker_pool	*pool;		
	struct workqueue_struct *wq;		
	int			work_color;	
	int			flush_color;	
	int			refcnt;		
	int			nr_in_flight[WORK_NR_COLORS];
						

	
	int			nr_active;	
	int			max_active;	
	struct list_head	inactive_works;	
	struct list_head	pwqs_node;	
	struct list_head	mayday_node;	

	
	struct work_struct	unbound_release_work;
	struct rcu_head		rcu;
} __aligned(1 << WORK_STRUCT_FLAG_BITS);

struct wq_flusher {
	struct list_head	list;		
	int			flush_color;	
	struct completion	done;		
};

struct wq_device;

struct workqueue_struct {
	struct list_head	pwqs;		
	struct list_head	list;		

	struct mutex		mutex;		
	int			work_color;	
	int			flush_color;	
	atomic_t		nr_pwqs_to_flush; 
	struct wq_flusher	*first_flusher;	
	struct list_head	flusher_queue;	
	struct list_head	flusher_overflow; 

	struct list_head	maydays;	
	struct worker		*rescuer;	

	int			nr_drainers;	
	int			saved_max_active; 

	struct workqueue_attrs	*unbound_attrs;	
	struct pool_workqueue	*dfl_pwq;	

	char			name[WQ_NAME_LEN]; 

	
	struct rcu_head		rcu;

	
	unsigned int		flags ____cacheline_aligned; 
	struct pool_workqueue __percpu *cpu_pwqs; 
	struct pool_workqueue __rcu *numa_pwq_tbl[]; 
};

static struct kmem_cache *pwq_cache;

static cpumask_var_t *wq_numa_possible_cpumask;
					

static bool wq_disable_numa;
module_param_named(disable_numa, wq_disable_numa, bool, 0444);

static bool wq_power_efficient = IS_ENABLED(CONFIG_WQ_POWER_EFFICIENT_DEFAULT);
module_param_named(power_efficient, wq_power_efficient, bool, 0444);

static bool wq_online;			

static bool wq_numa_enabled;		

static struct workqueue_attrs *wq_update_unbound_numa_attrs_buf;

static DEFINE_MUTEX(wq_pool_mutex);	
static DEFINE_MUTEX(wq_pool_attach_mutex); 
static DEFINE_RAW_SPINLOCK(wq_mayday_lock);	

static struct rcuwait manager_wait = __RCUWAIT_INITIALIZER(manager_wait);

static LIST_HEAD(workqueues);		
static bool workqueue_freezing;		

static cpumask_var_t wq_unbound_cpumask;

static DEFINE_PER_CPU(int, wq_rr_cpu_last);

static bool wq_debug_force_rr_cpu = false;
module_param_named(debug_force_rr_cpu, wq_debug_force_rr_cpu, bool, 0644);

static DEFINE_PER_CPU_SHARED_ALIGNED(struct worker_pool [NR_STD_WORKER_POOLS], cpu_worker_pools);

static DEFINE_IDR(worker_pool_idr);	

static DEFINE_HASHTABLE(unbound_pool_hash, UNBOUND_POOL_HASH_ORDER);

static struct workqueue_attrs *unbound_std_wq_attrs[NR_STD_WORKER_POOLS];

static struct workqueue_attrs *ordered_wq_attrs[NR_STD_WORKER_POOLS];

struct workqueue_struct *system_wq __read_mostly;
EXPORT_SYMBOL(system_wq);
struct workqueue_struct *system_highpri_wq __read_mostly;
EXPORT_SYMBOL_GPL(system_highpri_wq);
struct workqueue_struct *system_long_wq __read_mostly;
EXPORT_SYMBOL_GPL(system_long_wq);
struct workqueue_struct *system_unbound_wq __read_mostly;
EXPORT_SYMBOL_GPL(system_unbound_wq);
struct workqueue_struct *system_freezable_wq __read_mostly;
EXPORT_SYMBOL_GPL(system_freezable_wq);
struct workqueue_struct *system_power_efficient_wq __read_mostly;
EXPORT_SYMBOL_GPL(system_power_efficient_wq);
struct workqueue_struct *system_freezable_power_efficient_wq __read_mostly;
EXPORT_SYMBOL_GPL(system_freezable_power_efficient_wq);

static int worker_thread(void *__worker);
static void workqueue_sysfs_unregister(struct workqueue_struct *wq);
static void show_pwq(struct pool_workqueue *pwq);

#define assert_rcu_or_pool_mutex()					\
	RCU_LOCKDEP_WARN(!rcu_read_lock_held() &&			\
			 !lockdep_is_held(&wq_pool_mutex),		\
			 "RCU or wq_pool_mutex should be held")

#define assert_rcu_or_wq_mutex_or_pool_mutex(wq)			\
	RCU_LOCKDEP_WARN(!rcu_read_lock_held() &&			\
			 !lockdep_is_held(&wq->mutex) &&		\
			 !lockdep_is_held(&wq_pool_mutex),		\
			 "RCU, wq->mutex or wq_pool_mutex should be held")

#define for_each_cpu_worker_pool(pool, cpu)				\
	for ((pool) = &per_cpu(cpu_worker_pools, cpu)[0];		\
	     (pool) < &per_cpu(cpu_worker_pools, cpu)[NR_STD_WORKER_POOLS]; \
	     (pool)++)

#define for_each_pool(pool, pi)						\
	idr_for_each_entry(&worker_pool_idr, pool, pi)			\
		if (({ assert_rcu_or_pool_mutex(); false; })) { }	\
		else

#define for_each_pool_worker(worker, pool)				\
	list_for_each_entry((worker), &(pool)->workers, node)		\
		if (({ lockdep_assert_held(&wq_pool_attach_mutex); false; })) { } \
		else

#define for_each_pwq(pwq, wq)						\
	list_for_each_entry_rcu((pwq), &(wq)->pwqs, pwqs_node,		\
				 lockdep_is_held(&(wq->mutex)))

static inline void debug_work_activate(struct work_struct *work) { }
static inline void debug_work_deactivate(struct work_struct *work) { }

static int worker_pool_assign_id(struct worker_pool *pool)
{
	int ret;

	lockdep_assert_held(&wq_pool_mutex);

	ret = idr_alloc(&worker_pool_idr, pool, 0, WORK_OFFQ_POOL_NONE,
			GFP_KERNEL);
	if (ret >= 0) {
		pool->id = ret;
		return 0;
	}
	return ret;
}

static struct pool_workqueue *unbound_pwq_by_node(struct workqueue_struct *wq,
						  int node)
{
	assert_rcu_or_wq_mutex_or_pool_mutex(wq);

	
	if (unlikely(node == NUMA_NO_NODE))
		return wq->dfl_pwq;

	return rcu_dereference_raw(wq->numa_pwq_tbl[node]);
}

static unsigned int work_color_to_flags(int color)
{
	return color << WORK_STRUCT_COLOR_SHIFT;
}

static int get_work_color(unsigned long work_data)
{
	return (work_data >> WORK_STRUCT_COLOR_SHIFT) &
		((1 << WORK_STRUCT_COLOR_BITS) - 1);
}

static int work_next_color(int color)
{
	return (color + 1) % WORK_NR_COLORS;
}

static inline void set_work_data(struct work_struct *work, unsigned long data,
				 unsigned long flags)
{
	WARN_ON_ONCE(!work_pending(work));
	atomic_long_set(&work->data, data | flags | work_static(work));
}

static void set_work_pwq(struct work_struct *work, struct pool_workqueue *pwq,
			 unsigned long extra_flags)
{
	set_work_data(work, (unsigned long)pwq,
		      WORK_STRUCT_PENDING | WORK_STRUCT_PWQ | extra_flags);
}

static void set_work_pool_and_keep_pending(struct work_struct *work,
					   int pool_id)
{
	set_work_data(work, (unsigned long)pool_id << WORK_OFFQ_POOL_SHIFT,
		      WORK_STRUCT_PENDING);
}

static void set_work_pool_and_clear_pending(struct work_struct *work,
					    int pool_id)
{
	
	smp_wmb();
	set_work_data(work, (unsigned long)pool_id << WORK_OFFQ_POOL_SHIFT, 0);
	
	smp_mb();
}

static void clear_work_data(struct work_struct *work)
{
	smp_wmb();	
	set_work_data(work, WORK_STRUCT_NO_POOL, 0);
}

static struct pool_workqueue *get_work_pwq(struct work_struct *work)
{
	unsigned long data = atomic_long_read(&work->data);

	if (data & WORK_STRUCT_PWQ)
		return (void *)(data & WORK_STRUCT_WQ_DATA_MASK);
	else
		return NULL;
}

static struct worker_pool *get_work_pool(struct work_struct *work)
{
	unsigned long data = atomic_long_read(&work->data);
	int pool_id;

	assert_rcu_or_pool_mutex();

	if (data & WORK_STRUCT_PWQ)
		return ((struct pool_workqueue *)
			(data & WORK_STRUCT_WQ_DATA_MASK))->pool;

	pool_id = data >> WORK_OFFQ_POOL_SHIFT;
	if (pool_id == WORK_OFFQ_POOL_NONE)
		return NULL;

	return idr_find(&worker_pool_idr, pool_id);
}

static int get_work_pool_id(struct work_struct *work)
{
	unsigned long data = atomic_long_read(&work->data);

	if (data & WORK_STRUCT_PWQ)
		return ((struct pool_workqueue *)
			(data & WORK_STRUCT_WQ_DATA_MASK))->pool->id;

	return data >> WORK_OFFQ_POOL_SHIFT;
}

static void mark_work_canceling(struct work_struct *work)
{
	unsigned long pool_id = get_work_pool_id(work);

	pool_id <<= WORK_OFFQ_POOL_SHIFT;
	set_work_data(work, pool_id | WORK_OFFQ_CANCELING, WORK_STRUCT_PENDING);
}

static bool work_is_canceling(struct work_struct *work)
{
	unsigned long data = atomic_long_read(&work->data);

	return !(data & WORK_STRUCT_PWQ) && (data & WORK_OFFQ_CANCELING);
}

static bool __need_more_worker(struct worker_pool *pool)
{
	return !pool->nr_running;
}

static bool need_more_worker(struct worker_pool *pool)
{
	return !list_empty(&pool->worklist) && __need_more_worker(pool);
}

static bool may_start_working(struct worker_pool *pool)
{
	return pool->nr_idle;
}

static bool keep_working(struct worker_pool *pool)
{
	return !list_empty(&pool->worklist) && (pool->nr_running <= 1);
}

static bool need_to_create_worker(struct worker_pool *pool)
{
	return need_more_worker(pool) && !may_start_working(pool);
}

static bool too_many_workers(struct worker_pool *pool)
{
	bool managing = pool->flags & POOL_MANAGER_ACTIVE;
	int nr_idle = pool->nr_idle + managing; 
	int nr_busy = pool->nr_workers - nr_idle;

	return nr_idle > 2 && (nr_idle - 2) * MAX_IDLE_WORKERS_RATIO >= nr_busy;
}

static struct worker *first_idle_worker(struct worker_pool *pool)
{
	if (unlikely(list_empty(&pool->idle_list)))
		return NULL;

	return list_first_entry(&pool->idle_list, struct worker, entry);
}

static void wake_up_worker(struct worker_pool *pool)
{
	struct worker *worker = first_idle_worker(pool);

	if (likely(worker))
		wake_up_process(worker->task);
}

void wq_worker_running(struct task_struct *task)
{
	struct worker *worker = kthread_data(task);

	if (!worker->sleeping)
		return;

	
	preempt_disable();
	if (!(worker->flags & WORKER_NOT_RUNNING))
		worker->pool->nr_running++;
	preempt_enable();
	worker->sleeping = 0;
}

void wq_worker_sleeping(struct task_struct *task)
{
	struct worker *worker = kthread_data(task);
	struct worker_pool *pool;

	
	if (worker->flags & WORKER_NOT_RUNNING)
		return;

	pool = worker->pool;

	
	if (worker->sleeping)
		return;

	worker->sleeping = 1;
	raw_spin_lock_irq(&pool->lock);

	
	if (worker->flags & WORKER_NOT_RUNNING) {
		raw_spin_unlock_irq(&pool->lock);
		return;
	}

	pool->nr_running--;
	if (need_more_worker(pool))
		wake_up_worker(pool);
	raw_spin_unlock_irq(&pool->lock);
}

work_func_t wq_worker_last_func(struct task_struct *task)
{
	struct worker *worker = kthread_data(task);

	return worker->last_func;
}

static inline void worker_set_flags(struct worker *worker, unsigned int flags)
{
	struct worker_pool *pool = worker->pool;

	WARN_ON_ONCE(worker->task != current);

	
	if ((flags & WORKER_NOT_RUNNING) &&
	    !(worker->flags & WORKER_NOT_RUNNING)) {
		pool->nr_running--;
	}

	worker->flags |= flags;
}

static inline void worker_clr_flags(struct worker *worker, unsigned int flags)
{
	struct worker_pool *pool = worker->pool;
	unsigned int oflags = worker->flags;

	WARN_ON_ONCE(worker->task != current);

	worker->flags &= ~flags;

	
	if ((flags & WORKER_NOT_RUNNING) && (oflags & WORKER_NOT_RUNNING))
		if (!(worker->flags & WORKER_NOT_RUNNING))
			pool->nr_running++;
}

static struct worker *find_worker_executing_work(struct worker_pool *pool,
						 struct work_struct *work)
{
	struct worker *worker;

	hash_for_each_possible(pool->busy_hash, worker, hentry,
			       (unsigned long)work)
		if (worker->current_work == work &&
		    worker->current_func == work->func)
			return worker;

	return NULL;
}

static void move_linked_works(struct work_struct *work, struct list_head *head,
			      struct work_struct **nextp)
{
	struct work_struct *n;

	
	list_for_each_entry_safe_from(work, n, NULL, entry) {
		list_move_tail(&work->entry, head);
		if (!(*work_data_bits(work) & WORK_STRUCT_LINKED))
			break;
	}

	
	if (nextp)
		*nextp = n;
}

static void get_pwq(struct pool_workqueue *pwq)
{
	lockdep_assert_held(&pwq->pool->lock);
	WARN_ON_ONCE(pwq->refcnt <= 0);
	pwq->refcnt++;
}

static void put_pwq(struct pool_workqueue *pwq)
{
	lockdep_assert_held(&pwq->pool->lock);
	if (likely(--pwq->refcnt))
		return;
	if (WARN_ON_ONCE(!(pwq->wq->flags & WQ_UNBOUND)))
		return;
	
	schedule_work(&pwq->unbound_release_work);
}

static void put_pwq_unlocked(struct pool_workqueue *pwq)
{
	if (pwq) {
		
		raw_spin_lock_irq(&pwq->pool->lock);
		put_pwq(pwq);
		raw_spin_unlock_irq(&pwq->pool->lock);
	}
}

static void pwq_activate_inactive_work(struct work_struct *work)
{
	struct pool_workqueue *pwq = get_work_pwq(work);

	
	if (list_empty(&pwq->pool->worklist))
		pwq->pool->watchdog_ts = jiffies;
	move_linked_works(work, &pwq->pool->worklist, NULL);
	__clear_bit(WORK_STRUCT_INACTIVE_BIT, work_data_bits(work));
	pwq->nr_active++;
}

static void pwq_activate_first_inactive(struct pool_workqueue *pwq)
{
	struct work_struct *work = list_first_entry(&pwq->inactive_works,
						    struct work_struct, entry);

	pwq_activate_inactive_work(work);
}

static void pwq_dec_nr_in_flight(struct pool_workqueue *pwq, unsigned long work_data)
{
	int color = get_work_color(work_data);

	if (!(work_data & WORK_STRUCT_INACTIVE)) {
		pwq->nr_active--;
		if (!list_empty(&pwq->inactive_works)) {
			
			if (pwq->nr_active < pwq->max_active)
				pwq_activate_first_inactive(pwq);
		}
	}

	pwq->nr_in_flight[color]--;

	
	if (likely(pwq->flush_color != color))
		goto out_put;

	
	if (pwq->nr_in_flight[color])
		goto out_put;

	
	pwq->flush_color = -1;

	
	if (atomic_dec_and_test(&pwq->wq->nr_pwqs_to_flush))
		complete(&pwq->wq->first_flusher->done);
out_put:
	put_pwq(pwq);
}

static int try_to_grab_pending(struct work_struct *work, bool is_dwork,
			       unsigned long *flags)
{
	struct worker_pool *pool;
	struct pool_workqueue *pwq;

	local_irq_save(*flags);

	
	if (is_dwork) {
		struct delayed_work *dwork = to_delayed_work(work);

		
		if (likely(del_timer(&dwork->timer)))
			return 1;
	}

	
	if (!test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work)))
		return 0;

	rcu_read_lock();
	
	pool = get_work_pool(work);
	if (!pool)
		goto fail;

	raw_spin_lock(&pool->lock);
	
	pwq = get_work_pwq(work);
	if (pwq && pwq->pool == pool) {
		debug_work_deactivate(work);

		
		if (*work_data_bits(work) & WORK_STRUCT_INACTIVE)
			pwq_activate_inactive_work(work);

		list_del_init(&work->entry);
		pwq_dec_nr_in_flight(pwq, *work_data_bits(work));

		
		set_work_pool_and_keep_pending(work, pool->id);

		raw_spin_unlock(&pool->lock);
		rcu_read_unlock();
		return 1;
	}
	raw_spin_unlock(&pool->lock);
fail:
	rcu_read_unlock();
	local_irq_restore(*flags);
	if (work_is_canceling(work))
		return -ENOENT;
	cpu_relax();
	return -EAGAIN;
}

static void insert_work(struct pool_workqueue *pwq, struct work_struct *work,
			struct list_head *head, unsigned int extra_flags)
{
	struct worker_pool *pool = pwq->pool;

	
	kasan_record_aux_stack_noalloc(work);

	
	set_work_pwq(work, pwq, extra_flags);
	list_add_tail(&work->entry, head);
	get_pwq(pwq);

	if (__need_more_worker(pool))
		wake_up_worker(pool);
}

static bool is_chained_work(struct workqueue_struct *wq)
{
	struct worker *worker;

	worker = current_wq_worker();
	
	return worker && worker->current_pwq->wq == wq;
}

static int wq_select_unbound_cpu(int cpu)
{
	static bool printed_dbg_warning;
	int new_cpu;

	if (likely(!wq_debug_force_rr_cpu)) {
		if (cpumask_test_cpu(cpu, wq_unbound_cpumask))
			return cpu;
	} else if (!printed_dbg_warning) {
		pr_warn("workqueue: round-robin CPU selection forced, expect performance impact\n");
		printed_dbg_warning = true;
	}

	if (cpumask_empty(wq_unbound_cpumask))
		return cpu;

	new_cpu = __this_cpu_read(wq_rr_cpu_last);
	new_cpu = cpumask_next_and(new_cpu, wq_unbound_cpumask, cpu_online_mask);
	if (unlikely(new_cpu >= nr_cpu_ids)) {
		new_cpu = cpumask_first_and(wq_unbound_cpumask, cpu_online_mask);
		if (unlikely(new_cpu >= nr_cpu_ids))
			return cpu;
	}
	__this_cpu_write(wq_rr_cpu_last, new_cpu);

	return new_cpu;
}

static void __queue_work(int cpu, struct workqueue_struct *wq,
			 struct work_struct *work)
{
	struct pool_workqueue *pwq;
	struct worker_pool *last_pool;
	struct list_head *worklist;
	unsigned int work_flags;
	unsigned int req_cpu = cpu;

	
	lockdep_assert_irqs_disabled();

	
	if (unlikely(wq->flags & __WQ_DRAINING) &&
	    WARN_ON_ONCE(!is_chained_work(wq)))
		return;
	rcu_read_lock();
retry:
	
	if (wq->flags & WQ_UNBOUND) {
		if (req_cpu == WORK_CPU_UNBOUND)
			cpu = wq_select_unbound_cpu(raw_smp_processor_id());
		pwq = unbound_pwq_by_node(wq, cpu_to_node(cpu));
	} else {
		if (req_cpu == WORK_CPU_UNBOUND)
			cpu = raw_smp_processor_id();
		pwq = per_cpu_ptr(wq->cpu_pwqs, cpu);
	}

	
	last_pool = get_work_pool(work);
	if (last_pool && last_pool != pwq->pool) {
		struct worker *worker;

		raw_spin_lock(&last_pool->lock);

		worker = find_worker_executing_work(last_pool, work);

		if (worker && worker->current_pwq->wq == wq) {
			pwq = worker->current_pwq;
		} else {
			
			raw_spin_unlock(&last_pool->lock);
			raw_spin_lock(&pwq->pool->lock);
		}
	} else {
		raw_spin_lock(&pwq->pool->lock);
	}

	
	if (unlikely(!pwq->refcnt)) {
		if (wq->flags & WQ_UNBOUND) {
			raw_spin_unlock(&pwq->pool->lock);
			cpu_relax();
			goto retry;
		}
		
		WARN_ONCE(true, "workqueue: per-cpu pwq for %s on cpu%d has 0 refcnt",
			  wq->name, cpu);
	}

	
	

	if (WARN_ON(!list_empty(&work->entry)))
		goto out;

	pwq->nr_in_flight[pwq->work_color]++;
	work_flags = work_color_to_flags(pwq->work_color);

	if (likely(pwq->nr_active < pwq->max_active)) {
		
		pwq->nr_active++;
		worklist = &pwq->pool->worklist;
		if (list_empty(worklist))
			pwq->pool->watchdog_ts = jiffies;
	} else {
		work_flags |= WORK_STRUCT_INACTIVE;
		worklist = &pwq->inactive_works;
	}

	debug_work_activate(work);
	insert_work(pwq, work, worklist, work_flags);

out:
	raw_spin_unlock(&pwq->pool->lock);
	rcu_read_unlock();
}

bool queue_work_on(int cpu, struct workqueue_struct *wq,
		   struct work_struct *work)
{
	bool ret = false;
	unsigned long flags;

	local_irq_save(flags);

	if (!test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work))) {
		__queue_work(cpu, wq, work);
		ret = true;
	}

	local_irq_restore(flags);
	return ret;
}
EXPORT_SYMBOL(queue_work_on);

static int workqueue_select_cpu_near(int node)
{
	int cpu;

	
	if (!wq_numa_enabled)
		return WORK_CPU_UNBOUND;

	
	if (node < 0 || node >= MAX_NUMNODES || !node_online(node))
		return WORK_CPU_UNBOUND;

	
	cpu = raw_smp_processor_id();
	if (node == cpu_to_node(cpu))
		return cpu;

	
	cpu = cpumask_any_and(cpumask_of_node(node), cpu_online_mask);

	
	return cpu < nr_cpu_ids ? cpu : WORK_CPU_UNBOUND;
}

bool queue_work_node(int node, struct workqueue_struct *wq,
		     struct work_struct *work)
{
	unsigned long flags;
	bool ret = false;

	
	WARN_ON_ONCE(!(wq->flags & WQ_UNBOUND));

	local_irq_save(flags);

	if (!test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work))) {
		int cpu = workqueue_select_cpu_near(node);

		__queue_work(cpu, wq, work);
		ret = true;
	}

	local_irq_restore(flags);
	return ret;
}
EXPORT_SYMBOL_GPL(queue_work_node);

void delayed_work_timer_fn(struct timer_list *t)
{
	struct delayed_work *dwork = from_timer(dwork, t, timer);

	
	__queue_work(dwork->cpu, dwork->wq, &dwork->work);
}
EXPORT_SYMBOL(delayed_work_timer_fn);

static void __queue_delayed_work(int cpu, struct workqueue_struct *wq,
				struct delayed_work *dwork, unsigned long delay)
{
	struct timer_list *timer = &dwork->timer;
	struct work_struct *work = &dwork->work;

	WARN_ON_ONCE(!wq);
	WARN_ON_FUNCTION_MISMATCH(timer->function, delayed_work_timer_fn);
	WARN_ON_ONCE(timer_pending(timer));
	WARN_ON_ONCE(!list_empty(&work->entry));

	
	if (!delay) {
		__queue_work(cpu, wq, &dwork->work);
		return;
	}

	dwork->wq = wq;
	dwork->cpu = cpu;
	timer->expires = jiffies + delay;

	if (unlikely(cpu != WORK_CPU_UNBOUND))
		add_timer_on(timer, cpu);
	else
		add_timer(timer);
}

bool queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
			   struct delayed_work *dwork, unsigned long delay)
{
	struct work_struct *work = &dwork->work;
	bool ret = false;
	unsigned long flags;

	
	local_irq_save(flags);

	if (!test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work))) {
		__queue_delayed_work(cpu, wq, dwork, delay);
		ret = true;
	}

	local_irq_restore(flags);
	return ret;
}
EXPORT_SYMBOL(queue_delayed_work_on);

bool mod_delayed_work_on(int cpu, struct workqueue_struct *wq,
			 struct delayed_work *dwork, unsigned long delay)
{
	unsigned long flags;
	int ret;

	do {
		ret = try_to_grab_pending(&dwork->work, true, &flags);
	} while (unlikely(ret == -EAGAIN));

	if (likely(ret >= 0)) {
		__queue_delayed_work(cpu, wq, dwork, delay);
		local_irq_restore(flags);
	}

	
	return ret;
}
EXPORT_SYMBOL_GPL(mod_delayed_work_on);

static void rcu_work_rcufn(struct rcu_head *rcu)
{
	struct rcu_work *rwork = container_of(rcu, struct rcu_work, rcu);

	
	local_irq_disable();
	__queue_work(WORK_CPU_UNBOUND, rwork->wq, &rwork->work);
	local_irq_enable();
}

bool queue_rcu_work(struct workqueue_struct *wq, struct rcu_work *rwork)
{
	struct work_struct *work = &rwork->work;

	if (!test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work))) {
		rwork->wq = wq;
		call_rcu(&rwork->rcu, rcu_work_rcufn);
		return true;
	}

	return false;
}
EXPORT_SYMBOL(queue_rcu_work);

static void worker_enter_idle(struct worker *worker)
{
	struct worker_pool *pool = worker->pool;

	if (WARN_ON_ONCE(worker->flags & WORKER_IDLE) ||
	    WARN_ON_ONCE(!list_empty(&worker->entry) &&
			 (worker->hentry.next || worker->hentry.pprev)))
		return;

	
	worker->flags |= WORKER_IDLE;
	pool->nr_idle++;
	worker->last_active = jiffies;

	
	list_add(&worker->entry, &pool->idle_list);

	if (too_many_workers(pool) && !timer_pending(&pool->idle_timer))
		mod_timer(&pool->idle_timer, jiffies + IDLE_WORKER_TIMEOUT);

	
	WARN_ON_ONCE(pool->nr_workers == pool->nr_idle && pool->nr_running);
}

static void worker_leave_idle(struct worker *worker)
{
	struct worker_pool *pool = worker->pool;

	if (WARN_ON_ONCE(!(worker->flags & WORKER_IDLE)))
		return;
	worker_clr_flags(worker, WORKER_IDLE);
	pool->nr_idle--;
	list_del_init(&worker->entry);
}

static struct worker *alloc_worker(int node)
{
	struct worker *worker;

	worker = kzalloc_node(sizeof(*worker), GFP_KERNEL, node);
	if (worker) {
		INIT_LIST_HEAD(&worker->entry);
		INIT_LIST_HEAD(&worker->scheduled);
		INIT_LIST_HEAD(&worker->node);
		
		worker->flags = WORKER_PREP;
	}
	return worker;
}

static void worker_attach_to_pool(struct worker *worker,
				   struct worker_pool *pool)
{
	mutex_lock(&wq_pool_attach_mutex);

	
	if (pool->flags & POOL_DISASSOCIATED)
		worker->flags |= WORKER_UNBOUND;
	else
		kthread_set_per_cpu(worker->task, pool->cpu);

	if (worker->rescue_wq)
		set_cpus_allowed_ptr(worker->task, pool->attrs->cpumask);

	list_add_tail(&worker->node, &pool->workers);
	worker->pool = pool;

	mutex_unlock(&wq_pool_attach_mutex);
}

static void worker_detach_from_pool(struct worker *worker)
{
	struct worker_pool *pool = worker->pool;
	struct completion *detach_completion = NULL;

	mutex_lock(&wq_pool_attach_mutex);

	kthread_set_per_cpu(worker->task, -1);
	list_del(&worker->node);
	worker->pool = NULL;

	if (list_empty(&pool->workers))
		detach_completion = pool->detach_completion;
	mutex_unlock(&wq_pool_attach_mutex);

	
	worker->flags &= ~(WORKER_UNBOUND | WORKER_REBOUND);

	if (detach_completion)
		complete(detach_completion);
}

static struct worker *create_worker(struct worker_pool *pool)
{
	struct worker *worker;
	int id;
	char id_buf[16];

	
	id = ida_alloc(&pool->worker_ida, GFP_KERNEL);
	if (id < 0)
		return NULL;

	worker = alloc_worker(pool->node);
	if (!worker)
		goto fail;

	worker->id = id;

	if (pool->cpu >= 0)
		snprintf(id_buf, sizeof(id_buf), "%d:%d%s", pool->cpu, id,
			 pool->attrs->nice < 0  ? "H" : "");
	else
		snprintf(id_buf, sizeof(id_buf), "u%d:%d", pool->id, id);

	worker->task = kthread_create_on_node(worker_thread, worker, pool->node,
					      "kworker/%s", id_buf);
	if (IS_ERR(worker->task))
		goto fail;

	set_user_nice(worker->task, pool->attrs->nice);
	kthread_bind_mask(worker->task, pool->attrs->cpumask);

	
	worker_attach_to_pool(worker, pool);

	
	raw_spin_lock_irq(&pool->lock);
	worker->pool->nr_workers++;
	worker_enter_idle(worker);
	wake_up_process(worker->task);
	raw_spin_unlock_irq(&pool->lock);

	return worker;

fail:
	ida_free(&pool->worker_ida, id);
	kfree(worker);
	return NULL;
}

static void destroy_worker(struct worker *worker)
{
	struct worker_pool *pool = worker->pool;

	lockdep_assert_held(&pool->lock);

	
	if (WARN_ON(worker->current_work) ||
	    WARN_ON(!list_empty(&worker->scheduled)) ||
	    WARN_ON(!(worker->flags & WORKER_IDLE)))
		return;

	pool->nr_workers--;
	pool->nr_idle--;

	list_del_init(&worker->entry);
	worker->flags |= WORKER_DIE;
	wake_up_process(worker->task);
}

static void idle_worker_timeout(struct timer_list *t)
{
	struct worker_pool *pool = from_timer(pool, t, idle_timer);

	raw_spin_lock_irq(&pool->lock);

	while (too_many_workers(pool)) {
		struct worker *worker;
		unsigned long expires;

		
		worker = list_entry(pool->idle_list.prev, struct worker, entry);
		expires = worker->last_active + IDLE_WORKER_TIMEOUT;

		if (time_before(jiffies, expires)) {
			mod_timer(&pool->idle_timer, expires);
			break;
		}

		destroy_worker(worker);
	}

	raw_spin_unlock_irq(&pool->lock);
}

static void send_mayday(struct work_struct *work)
{
	struct pool_workqueue *pwq = get_work_pwq(work);
	struct workqueue_struct *wq = pwq->wq;

	lockdep_assert_held(&wq_mayday_lock);

	if (!wq->rescuer)
		return;

	
	if (list_empty(&pwq->mayday_node)) {
		
		get_pwq(pwq);
		list_add_tail(&pwq->mayday_node, &wq->maydays);
		wake_up_process(wq->rescuer->task);
	}
}

static void pool_mayday_timeout(struct timer_list *t)
{
	struct worker_pool *pool = from_timer(pool, t, mayday_timer);
	struct work_struct *work;

	raw_spin_lock_irq(&pool->lock);
	raw_spin_lock(&wq_mayday_lock);		

	if (need_to_create_worker(pool)) {
		
		list_for_each_entry(work, &pool->worklist, entry)
			send_mayday(work);
	}

	raw_spin_unlock(&wq_mayday_lock);
	raw_spin_unlock_irq(&pool->lock);

	mod_timer(&pool->mayday_timer, jiffies + MAYDAY_INTERVAL);
}

static void maybe_create_worker(struct worker_pool *pool)
__releases(&pool->lock)
__acquires(&pool->lock)
{
restart:
	raw_spin_unlock_irq(&pool->lock);

	
	mod_timer(&pool->mayday_timer, jiffies + MAYDAY_INITIAL_TIMEOUT);

	while (true) {
		if (create_worker(pool) || !need_to_create_worker(pool))
			break;

		schedule_timeout_interruptible(CREATE_COOLDOWN);

		if (!need_to_create_worker(pool))
			break;
	}

	del_timer_sync(&pool->mayday_timer);
	raw_spin_lock_irq(&pool->lock);
	
	if (need_to_create_worker(pool))
		goto restart;
}

static bool manage_workers(struct worker *worker)
{
	struct worker_pool *pool = worker->pool;

	if (pool->flags & POOL_MANAGER_ACTIVE)
		return false;

	pool->flags |= POOL_MANAGER_ACTIVE;
	pool->manager = worker;

	maybe_create_worker(pool);

	pool->manager = NULL;
	pool->flags &= ~POOL_MANAGER_ACTIVE;
	rcuwait_wake_up(&manager_wait);
	return true;
}

static void process_one_work(struct worker *worker, struct work_struct *work)
__releases(&pool->lock)
__acquires(&pool->lock)
{
	struct pool_workqueue *pwq = get_work_pwq(work);
	struct worker_pool *pool = worker->pool;
	bool cpu_intensive = pwq->wq->flags & WQ_CPU_INTENSIVE;
	unsigned long work_data;
	struct worker *collision;
	
	WARN_ON_ONCE(!(pool->flags & POOL_DISASSOCIATED) &&
		     raw_smp_processor_id() != pool->cpu);

	
	collision = find_worker_executing_work(pool, work);
	if (unlikely(collision)) {
		move_linked_works(work, &collision->scheduled, NULL);
		return;
	}

	
	debug_work_deactivate(work);
	hash_add(pool->busy_hash, &worker->hentry, (unsigned long)work);
	worker->current_work = work;
	worker->current_func = work->func;
	worker->current_pwq = pwq;
	work_data = *work_data_bits(work);
	worker->current_color = get_work_color(work_data);

	
	strscpy(worker->desc, pwq->wq->name, WORKER_DESC_LEN);

	list_del_init(&work->entry);

	
	if (unlikely(cpu_intensive))
		worker_set_flags(worker, WORKER_CPU_INTENSIVE);

	
	if (need_more_worker(pool))
		wake_up_worker(pool);

	
	set_work_pool_and_clear_pending(work, pool->id);

	raw_spin_unlock_irq(&pool->lock);

	lock_map_acquire(&pwq->wq->lockdep_map);
	lock_map_acquire(&lockdep_map);
	
	lockdep_invariant_state(true);
	
	worker->current_func(work);
	
	
	lock_map_release(&lockdep_map);
	lock_map_release(&pwq->wq->lockdep_map);

	if (unlikely(in_atomic() || lockdep_depth(current) > 0)) {
		pr_err("BUG: workqueue leaked lock or atomic: %s/0x%08x/%d\n"
		       "     last function: %ps\n",
		       current->comm, preempt_count(), task_pid_nr(current),
		       worker->current_func);
		debug_show_held_locks(current);
		dump_stack();
	}

	
	cond_resched();

	raw_spin_lock_irq(&pool->lock);

	
	if (unlikely(cpu_intensive))
		worker_clr_flags(worker, WORKER_CPU_INTENSIVE);

	
	worker->last_func = worker->current_func;

	
	hash_del(&worker->hentry);
	worker->current_work = NULL;
	worker->current_func = NULL;
	worker->current_pwq = NULL;
	worker->current_color = INT_MAX;
	pwq_dec_nr_in_flight(pwq, work_data);
}

static void process_scheduled_works(struct worker *worker)
{
	while (!list_empty(&worker->scheduled)) {
		struct work_struct *work = list_first_entry(&worker->scheduled,
						struct work_struct, entry);
		process_one_work(worker, work);
	}
}

static void set_pf_worker(bool val)
{
	mutex_lock(&wq_pool_attach_mutex);
	if (val)
		current->flags |= PF_WQ_WORKER;
	else
		current->flags &= ~PF_WQ_WORKER;
	mutex_unlock(&wq_pool_attach_mutex);
}

static int worker_thread(void *__worker)
{
	struct worker *worker = __worker;
	struct worker_pool *pool = worker->pool;

	
	set_pf_worker(true);
woke_up:
	raw_spin_lock_irq(&pool->lock);

	
	if (unlikely(worker->flags & WORKER_DIE)) {
		raw_spin_unlock_irq(&pool->lock);
		WARN_ON_ONCE(!list_empty(&worker->entry));
		set_pf_worker(false);

		set_task_comm(worker->task, "kworker/dying");
		ida_free(&pool->worker_ida, worker->id);
		worker_detach_from_pool(worker);
		kfree(worker);
		return 0;
	}

	worker_leave_idle(worker);
recheck:
	
	if (!need_more_worker(pool))
		goto sleep;

	
	if (unlikely(!may_start_working(pool)) && manage_workers(worker))
		goto recheck;

	
	WARN_ON_ONCE(!list_empty(&worker->scheduled));

	
	worker_clr_flags(worker, WORKER_PREP | WORKER_REBOUND);

	do {
		struct work_struct *work =
			list_first_entry(&pool->worklist,
					 struct work_struct, entry);

		pool->watchdog_ts = jiffies;

		if (likely(!(*work_data_bits(work) & WORK_STRUCT_LINKED))) {
			
			process_one_work(worker, work);
			if (unlikely(!list_empty(&worker->scheduled)))
				process_scheduled_works(worker);
		} else {
			move_linked_works(work, &worker->scheduled, NULL);
			process_scheduled_works(worker);
		}
	} while (keep_working(pool));

	worker_set_flags(worker, WORKER_PREP);
sleep:
	
	worker_enter_idle(worker);
	__set_current_state(TASK_IDLE);
	raw_spin_unlock_irq(&pool->lock);
	schedule();
	goto woke_up;
}

static int rescuer_thread(void *__rescuer)
{
	struct worker *rescuer = __rescuer;
	struct workqueue_struct *wq = rescuer->rescue_wq;
	struct list_head *scheduled = &rescuer->scheduled;
	bool should_stop;

	set_user_nice(current, RESCUER_NICE_LEVEL);

	
	set_pf_worker(true);
repeat:
	set_current_state(TASK_IDLE);

	
	should_stop = kthread_should_stop();

	
	raw_spin_lock_irq(&wq_mayday_lock);

	while (!list_empty(&wq->maydays)) {
		struct pool_workqueue *pwq = list_first_entry(&wq->maydays,
					struct pool_workqueue, mayday_node);
		struct worker_pool *pool = pwq->pool;
		struct work_struct *work, *n;
		bool first = true;

		__set_current_state(TASK_RUNNING);
		list_del_init(&pwq->mayday_node);

		raw_spin_unlock_irq(&wq_mayday_lock);

		worker_attach_to_pool(rescuer, pool);

		raw_spin_lock_irq(&pool->lock);

		
		WARN_ON_ONCE(!list_empty(scheduled));
		list_for_each_entry_safe(work, n, &pool->worklist, entry) {
			if (get_work_pwq(work) == pwq) {
				if (first)
					pool->watchdog_ts = jiffies;
				move_linked_works(work, scheduled, &n);
			}
			first = false;
		}

		if (!list_empty(scheduled)) {
			process_scheduled_works(rescuer);

			
			if (pwq->nr_active && need_to_create_worker(pool)) {
				raw_spin_lock(&wq_mayday_lock);
				
				if (wq->rescuer && list_empty(&pwq->mayday_node)) {
					get_pwq(pwq);
					list_add_tail(&pwq->mayday_node, &wq->maydays);
				}
				raw_spin_unlock(&wq_mayday_lock);
			}
		}

		
		put_pwq(pwq);

		
		if (need_more_worker(pool))
			wake_up_worker(pool);

		raw_spin_unlock_irq(&pool->lock);

		worker_detach_from_pool(rescuer);

		raw_spin_lock_irq(&wq_mayday_lock);
	}

	raw_spin_unlock_irq(&wq_mayday_lock);

	if (should_stop) {
		__set_current_state(TASK_RUNNING);
		set_pf_worker(false);
		return 0;
	}

	
	WARN_ON_ONCE(!(rescuer->flags & WORKER_NOT_RUNNING));
	schedule();
	goto repeat;
}

static void check_flush_dependency(struct workqueue_struct *target_wq,
				   struct work_struct *target_work)
{
	work_func_t target_func = target_work ? target_work->func : NULL;
	struct worker *worker;

	if (target_wq->flags & WQ_MEM_RECLAIM)
		return;

	worker = current_wq_worker();

	WARN_ONCE(current->flags & PF_MEMALLOC,
		  "workqueue: PF_MEMALLOC task %d(%s) is flushing !WQ_MEM_RECLAIM %s:%ps",
		  current->pid, current->comm, target_wq->name, target_func);
	WARN_ONCE(worker && ((worker->current_pwq->wq->flags &
			      (WQ_MEM_RECLAIM | __WQ_LEGACY)) == WQ_MEM_RECLAIM),
		  "workqueue: WQ_MEM_RECLAIM %s:%ps is flushing !WQ_MEM_RECLAIM %s:%ps",
		  worker->current_pwq->wq->name, worker->current_func,
		  target_wq->name, target_func);
}

struct wq_barrier {
	struct work_struct	work;
	struct completion	done;
	struct task_struct	*task;	
};

static void wq_barrier_func(struct work_struct *work)
{
	struct wq_barrier *barr = container_of(work, struct wq_barrier, work);
	complete(&barr->done);
}

static void insert_wq_barrier(struct pool_workqueue *pwq,
			      struct wq_barrier *barr,
			      struct work_struct *target, struct worker *worker)
{
	unsigned int work_flags = 0;
	unsigned int work_color;
	struct list_head *head;

	
	INIT_WORK_ONSTACK(&barr->work, wq_barrier_func);
	__set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(&barr->work));

	init_completion_map(&barr->done, &target->lockdep_map);

	barr->task = current;

	
	work_flags |= WORK_STRUCT_INACTIVE;

	
	if (worker) {
		head = worker->scheduled.next;
		work_color = worker->current_color;
	} else {
		unsigned long *bits = work_data_bits(target);

		head = target->entry.next;
		
		work_flags |= *bits & WORK_STRUCT_LINKED;
		work_color = get_work_color(*bits);
		__set_bit(WORK_STRUCT_LINKED_BIT, bits);
	}

	pwq->nr_in_flight[work_color]++;
	work_flags |= work_color_to_flags(work_color);

	debug_work_activate(&barr->work);
	insert_work(pwq, &barr->work, head, work_flags);
}

static bool flush_workqueue_prep_pwqs(struct workqueue_struct *wq,
				      int flush_color, int work_color)
{
	bool wait = false;
	struct pool_workqueue *pwq;

	if (flush_color >= 0) {
		WARN_ON_ONCE(atomic_read(&wq->nr_pwqs_to_flush));
		atomic_set(&wq->nr_pwqs_to_flush, 1);
	}

	for_each_pwq(pwq, wq) {
		struct worker_pool *pool = pwq->pool;

		raw_spin_lock_irq(&pool->lock);

		if (flush_color >= 0) {
			WARN_ON_ONCE(pwq->flush_color != -1);

			if (pwq->nr_in_flight[flush_color]) {
				pwq->flush_color = flush_color;
				atomic_inc(&wq->nr_pwqs_to_flush);
				wait = true;
			}
		}

		if (work_color >= 0) {
			WARN_ON_ONCE(work_color != work_next_color(pwq->work_color));
			pwq->work_color = work_color;
		}

		raw_spin_unlock_irq(&pool->lock);
	}

	if (flush_color >= 0 && atomic_dec_and_test(&wq->nr_pwqs_to_flush))
		complete(&wq->first_flusher->done);

	return wait;
}

void __flush_workqueue(struct workqueue_struct *wq)
{
	struct wq_flusher this_flusher = {
		.list = LIST_HEAD_INIT(this_flusher.list),
		.flush_color = -1,
		.done = COMPLETION_INITIALIZER_ONSTACK_MAP(this_flusher.done, wq->lockdep_map),
	};
	int next_color;

	if (WARN_ON(!wq_online))
		return;

	lock_map_acquire(&wq->lockdep_map);
	lock_map_release(&wq->lockdep_map);

	mutex_lock(&wq->mutex);

	
	next_color = work_next_color(wq->work_color);

	if (next_color != wq->flush_color) {
		
		WARN_ON_ONCE(!list_empty(&wq->flusher_overflow));
		this_flusher.flush_color = wq->work_color;
		wq->work_color = next_color;

		if (!wq->first_flusher) {
			
			WARN_ON_ONCE(wq->flush_color != this_flusher.flush_color);

			wq->first_flusher = &this_flusher;

			if (!flush_workqueue_prep_pwqs(wq, wq->flush_color,
						       wq->work_color)) {
				
				wq->flush_color = next_color;
				wq->first_flusher = NULL;
				goto out_unlock;
			}
		} else {
			
			WARN_ON_ONCE(wq->flush_color == this_flusher.flush_color);
			list_add_tail(&this_flusher.list, &wq->flusher_queue);
			flush_workqueue_prep_pwqs(wq, -1, wq->work_color);
		}
	} else {
		
		list_add_tail(&this_flusher.list, &wq->flusher_overflow);
	}

	check_flush_dependency(wq, NULL);

	mutex_unlock(&wq->mutex);

	wait_for_completion(&this_flusher.done);

	
	if (READ_ONCE(wq->first_flusher) != &this_flusher)
		return;

	mutex_lock(&wq->mutex);

	
	if (wq->first_flusher != &this_flusher)
		goto out_unlock;

	WRITE_ONCE(wq->first_flusher, NULL);

	WARN_ON_ONCE(!list_empty(&this_flusher.list));
	WARN_ON_ONCE(wq->flush_color != this_flusher.flush_color);

	while (true) {
		struct wq_flusher *next, *tmp;

		
		list_for_each_entry_safe(next, tmp, &wq->flusher_queue, list) {
			if (next->flush_color != wq->flush_color)
				break;
			list_del_init(&next->list);
			complete(&next->done);
		}

		WARN_ON_ONCE(!list_empty(&wq->flusher_overflow) &&
			     wq->flush_color != work_next_color(wq->work_color));

		
		wq->flush_color = work_next_color(wq->flush_color);

		
		if (!list_empty(&wq->flusher_overflow)) {
			
			list_for_each_entry(tmp, &wq->flusher_overflow, list)
				tmp->flush_color = wq->work_color;

			wq->work_color = work_next_color(wq->work_color);

			list_splice_tail_init(&wq->flusher_overflow,
					      &wq->flusher_queue);
			flush_workqueue_prep_pwqs(wq, -1, wq->work_color);
		}

		if (list_empty(&wq->flusher_queue)) {
			WARN_ON_ONCE(wq->flush_color != wq->work_color);
			break;
		}

		
		WARN_ON_ONCE(wq->flush_color == wq->work_color);
		WARN_ON_ONCE(wq->flush_color != next->flush_color);

		list_del_init(&next->list);
		wq->first_flusher = next;

		if (flush_workqueue_prep_pwqs(wq, wq->flush_color, -1))
			break;

		
		wq->first_flusher = NULL;
	}

out_unlock:
	mutex_unlock(&wq->mutex);
}
EXPORT_SYMBOL(__flush_workqueue);

void drain_workqueue(struct workqueue_struct *wq)
{
	unsigned int flush_cnt = 0;
	struct pool_workqueue *pwq;

	
	mutex_lock(&wq->mutex);
	if (!wq->nr_drainers++)
		wq->flags |= __WQ_DRAINING;
	mutex_unlock(&wq->mutex);
reflush:
	__flush_workqueue(wq);

	mutex_lock(&wq->mutex);

	for_each_pwq(pwq, wq) {
		bool drained;

		raw_spin_lock_irq(&pwq->pool->lock);
		drained = !pwq->nr_active && list_empty(&pwq->inactive_works);
		raw_spin_unlock_irq(&pwq->pool->lock);

		if (drained)
			continue;

		if (++flush_cnt == 10 ||
		    (flush_cnt % 100 == 0 && flush_cnt <= 1000))
			pr_warn("workqueue %s: %s() isn't complete after %u tries\n",
				wq->name, __func__, flush_cnt);

		mutex_unlock(&wq->mutex);
		goto reflush;
	}

	if (!--wq->nr_drainers)
		wq->flags &= ~__WQ_DRAINING;
	mutex_unlock(&wq->mutex);
}
EXPORT_SYMBOL_GPL(drain_workqueue);

static bool start_flush_work(struct work_struct *work, struct wq_barrier *barr,
			     bool from_cancel)
{
	struct worker *worker = NULL;
	struct worker_pool *pool;
	struct pool_workqueue *pwq;

	might_sleep();

	rcu_read_lock();
	pool = get_work_pool(work);
	if (!pool) {
		rcu_read_unlock();
		return false;
	}

	raw_spin_lock_irq(&pool->lock);
	
	pwq = get_work_pwq(work);
	if (pwq) {
		if (unlikely(pwq->pool != pool))
			goto already_gone;
	} else {
		worker = find_worker_executing_work(pool, work);
		if (!worker)
			goto already_gone;
		pwq = worker->current_pwq;
	}

	check_flush_dependency(pwq->wq, work);

	insert_wq_barrier(pwq, barr, work, worker);
	raw_spin_unlock_irq(&pool->lock);

	
	if (!from_cancel &&
	    (pwq->wq->saved_max_active == 1 || pwq->wq->rescuer)) {
		lock_map_acquire(&pwq->wq->lockdep_map);
		lock_map_release(&pwq->wq->lockdep_map);
	}
	rcu_read_unlock();
	return true;
already_gone:
	raw_spin_unlock_irq(&pool->lock);
	rcu_read_unlock();
	return false;
}

static bool __flush_work(struct work_struct *work, bool from_cancel)
{
	struct wq_barrier barr;

	if (WARN_ON(!wq_online))
		return false;

	if (WARN_ON(!work->func))
		return false;

	if (!from_cancel) {
		lock_map_acquire(&work->lockdep_map);
		lock_map_release(&work->lockdep_map);
	}

	if (start_flush_work(work, &barr, from_cancel)) {
		wait_for_completion(&barr.done);
		destroy_work_on_stack(&barr.work);
		return true;
	} else {
		return false;
	}
}

bool flush_work(struct work_struct *work)
{
	return __flush_work(work, false);
}
EXPORT_SYMBOL_GPL(flush_work);

struct cwt_wait {
	wait_queue_entry_t		wait;
	struct work_struct	*work;
};

static int cwt_wakefn(wait_queue_entry_t *wait, unsigned mode, int sync, void *key)
{
	struct cwt_wait *cwait = container_of(wait, struct cwt_wait, wait);

	if (cwait->work != key)
		return 0;
	return autoremove_wake_function(wait, mode, sync, key);
}

static bool __cancel_work_timer(struct work_struct *work, bool is_dwork)
{
	static DECLARE_WAIT_QUEUE_HEAD(cancel_waitq);
	unsigned long flags;
	int ret;

	do {
		ret = try_to_grab_pending(work, is_dwork, &flags);
		
		if (unlikely(ret == -ENOENT)) {
			struct cwt_wait cwait;

			init_wait(&cwait.wait);
			cwait.wait.func = cwt_wakefn;
			cwait.work = work;

			prepare_to_wait_exclusive(&cancel_waitq, &cwait.wait,
						  TASK_UNINTERRUPTIBLE);
			if (work_is_canceling(work))
				schedule();
			finish_wait(&cancel_waitq, &cwait.wait);
		}
	} while (unlikely(ret < 0));

	
	mark_work_canceling(work);
	local_irq_restore(flags);

	
	if (wq_online)
		__flush_work(work, true);

	clear_work_data(work);

	
	smp_mb();
	if (waitqueue_active(&cancel_waitq))
		__wake_up(&cancel_waitq, TASK_NORMAL, 1, work);

	return ret;
}

bool cancel_work_sync(struct work_struct *work)
{
	return __cancel_work_timer(work, false);
}
EXPORT_SYMBOL_GPL(cancel_work_sync);

bool flush_delayed_work(struct delayed_work *dwork)
{
	local_irq_disable();
	if (del_timer_sync(&dwork->timer))
		__queue_work(dwork->cpu, dwork->wq, &dwork->work);
	local_irq_enable();
	return flush_work(&dwork->work);
}
EXPORT_SYMBOL(flush_delayed_work);

bool flush_rcu_work(struct rcu_work *rwork)
{
	if (test_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(&rwork->work))) {
		rcu_barrier();
		flush_work(&rwork->work);
		return true;
	} else {
		return flush_work(&rwork->work);
	}
}
EXPORT_SYMBOL(flush_rcu_work);

static bool __cancel_work(struct work_struct *work, bool is_dwork)
{
	unsigned long flags;
	int ret;

	do {
		ret = try_to_grab_pending(work, is_dwork, &flags);
	} while (unlikely(ret == -EAGAIN));

	if (unlikely(ret < 0))
		return false;

	set_work_pool_and_clear_pending(work, get_work_pool_id(work));
	local_irq_restore(flags);
	return ret;
}

bool cancel_delayed_work(struct delayed_work *dwork)
{
	return __cancel_work(&dwork->work, true);
}
EXPORT_SYMBOL(cancel_delayed_work);

bool cancel_delayed_work_sync(struct delayed_work *dwork)
{
	return __cancel_work_timer(&dwork->work, true);
}
EXPORT_SYMBOL(cancel_delayed_work_sync);

int schedule_on_each_cpu(work_func_t func)
{
	int cpu;
	struct work_struct __percpu *works;

	works = alloc_percpu(struct work_struct);
	if (!works)
		return -ENOMEM;

	cpus_read_lock();

	for_each_online_cpu(cpu) {
		struct work_struct *work = per_cpu_ptr(works, cpu);

		INIT_WORK(work, func);
		schedule_work_on(cpu, work);
	}

	for_each_online_cpu(cpu)
		flush_work(per_cpu_ptr(works, cpu));

	cpus_read_unlock();
	free_percpu(works);
	return 0;
}

int execute_in_process_context(work_func_t fn, struct execute_work *ew)
{
	if (!in_interrupt()) {
		fn(&ew->work);
		return 0;
	}

	INIT_WORK(&ew->work, fn);
	schedule_work(&ew->work);

	return 1;
}
EXPORT_SYMBOL_GPL(execute_in_process_context);

void free_workqueue_attrs(struct workqueue_attrs *attrs)
{
	if (attrs) {
		free_cpumask_var(attrs->cpumask);
		kfree(attrs);
	}
}

struct workqueue_attrs *alloc_workqueue_attrs(void)
{
	struct workqueue_attrs *attrs;

	attrs = kzalloc(sizeof(*attrs), GFP_KERNEL);
	if (!attrs)
		goto fail;
	if (!alloc_cpumask_var(&attrs->cpumask, GFP_KERNEL))
		goto fail;

	cpumask_copy(attrs->cpumask, cpu_possible_mask);
	return attrs;
fail:
	free_workqueue_attrs(attrs);
	return NULL;
}

static void copy_workqueue_attrs(struct workqueue_attrs *to,
				 const struct workqueue_attrs *from)
{
	to->nice = from->nice;
	cpumask_copy(to->cpumask, from->cpumask);
	
	to->no_numa = from->no_numa;
}

static u32 wqattrs_hash(const struct workqueue_attrs *attrs)
{
	u32 hash = 0;

	hash = jhash_1word(attrs->nice, hash);
	hash = jhash(cpumask_bits(attrs->cpumask),
		     BITS_TO_LONGS(nr_cpumask_bits) * sizeof(long), hash);
	return hash;
}

static bool wqattrs_equal(const struct workqueue_attrs *a,
			  const struct workqueue_attrs *b)
{
	if (a->nice != b->nice)
		return false;
	if (!cpumask_equal(a->cpumask, b->cpumask))
		return false;
	return true;
}

static int init_worker_pool(struct worker_pool *pool)
{
	raw_spin_lock_init(&pool->lock);
	pool->id = -1;
	pool->cpu = -1;
	pool->node = NUMA_NO_NODE;
	pool->flags |= POOL_DISASSOCIATED;
	pool->watchdog_ts = jiffies;
	INIT_LIST_HEAD(&pool->worklist);
	INIT_LIST_HEAD(&pool->idle_list);
	hash_init(pool->busy_hash);

	timer_setup(&pool->idle_timer, idle_worker_timeout, TIMER_DEFERRABLE);

	timer_setup(&pool->mayday_timer, pool_mayday_timeout, 0);

	INIT_LIST_HEAD(&pool->workers);

	ida_init(&pool->worker_ida);
	INIT_HLIST_NODE(&pool->hash_node);
	pool->refcnt = 1;

	
	pool->attrs = alloc_workqueue_attrs();
	if (!pool->attrs)
		return -ENOMEM;
	return 0;
}

static void wq_init_lockdep(struct workqueue_struct *wq)
{
}

static void wq_unregister_lockdep(struct workqueue_struct *wq)
{
}

static void wq_free_lockdep(struct workqueue_struct *wq)
{
}

static void rcu_free_wq(struct rcu_head *rcu)
{
	struct workqueue_struct *wq =
		container_of(rcu, struct workqueue_struct, rcu);

	wq_free_lockdep(wq);

	if (!(wq->flags & WQ_UNBOUND))
		free_percpu(wq->cpu_pwqs);
	else
		free_workqueue_attrs(wq->unbound_attrs);

	kfree(wq);
}

static void rcu_free_pool(struct rcu_head *rcu)
{
	struct worker_pool *pool = container_of(rcu, struct worker_pool, rcu);

	ida_destroy(&pool->worker_ida);
	free_workqueue_attrs(pool->attrs);
	kfree(pool);
}

static bool wq_manager_inactive(struct worker_pool *pool)
{
	raw_spin_lock_irq(&pool->lock);

	if (pool->flags & POOL_MANAGER_ACTIVE) {
		raw_spin_unlock_irq(&pool->lock);
		return false;
	}
	return true;
}

static void put_unbound_pool(struct worker_pool *pool)
{
	DECLARE_COMPLETION_ONSTACK(detach_completion);
	struct worker *worker;

	lockdep_assert_held(&wq_pool_mutex);

	if (--pool->refcnt)
		return;

	
	if (WARN_ON(!(pool->cpu < 0)) ||
	    WARN_ON(!list_empty(&pool->worklist)))
		return;

	
	if (pool->id >= 0)
		idr_remove(&worker_pool_idr, pool->id);
	hash_del(&pool->hash_node);

	
	rcuwait_wait_event(&manager_wait, wq_manager_inactive(pool),
			   TASK_UNINTERRUPTIBLE);
	pool->flags |= POOL_MANAGER_ACTIVE;

	while ((worker = first_idle_worker(pool)))
		destroy_worker(worker);
	WARN_ON(pool->nr_workers || pool->nr_idle);
	raw_spin_unlock_irq(&pool->lock);

	mutex_lock(&wq_pool_attach_mutex);
	if (!list_empty(&pool->workers))
		pool->detach_completion = &detach_completion;
	mutex_unlock(&wq_pool_attach_mutex);

	if (pool->detach_completion)
		wait_for_completion(pool->detach_completion);

	
	del_timer_sync(&pool->idle_timer);
	del_timer_sync(&pool->mayday_timer);

	
	call_rcu(&pool->rcu, rcu_free_pool);
}

static struct worker_pool *get_unbound_pool(const struct workqueue_attrs *attrs)
{
	u32 hash = wqattrs_hash(attrs);
	struct worker_pool *pool;
	int node;
	int target_node = NUMA_NO_NODE;

	lockdep_assert_held(&wq_pool_mutex);

	
	hash_for_each_possible(unbound_pool_hash, pool, hash_node, hash) {
		if (wqattrs_equal(pool->attrs, attrs)) {
			pool->refcnt++;
			return pool;
		}
	}

	
	if (wq_numa_enabled) {
		for_each_node(node) {
			if (cpumask_subset(attrs->cpumask,
					   wq_numa_possible_cpumask[node])) {
				target_node = node;
				break;
			}
		}
	}

	
	pool = kzalloc_node(sizeof(*pool), GFP_KERNEL, target_node);
	if (!pool || init_worker_pool(pool) < 0)
		goto fail;

	lockdep_set_subclass(&pool->lock, 1);	
	copy_workqueue_attrs(pool->attrs, attrs);
	pool->node = target_node;

	
	pool->attrs->no_numa = false;

	if (worker_pool_assign_id(pool) < 0)
		goto fail;

	
	if (wq_online && !create_worker(pool))
		goto fail;

	
	hash_add(unbound_pool_hash, &pool->hash_node, hash);

	return pool;
fail:
	if (pool)
		put_unbound_pool(pool);
	return NULL;
}

static void rcu_free_pwq(struct rcu_head *rcu)
{
	kmem_cache_free(pwq_cache,
			container_of(rcu, struct pool_workqueue, rcu));
}

static void pwq_unbound_release_workfn(struct work_struct *work)
{
	struct pool_workqueue *pwq = container_of(work, struct pool_workqueue,
						  unbound_release_work);
	struct workqueue_struct *wq = pwq->wq;
	struct worker_pool *pool = pwq->pool;
	bool is_last = false;

	
	if (!list_empty(&pwq->pwqs_node)) {
		if (WARN_ON_ONCE(!(wq->flags & WQ_UNBOUND)))
			return;

		mutex_lock(&wq->mutex);
		list_del_rcu(&pwq->pwqs_node);
		is_last = list_empty(&wq->pwqs);
		mutex_unlock(&wq->mutex);
	}

	mutex_lock(&wq_pool_mutex);
	put_unbound_pool(pool);
	mutex_unlock(&wq_pool_mutex);

	call_rcu(&pwq->rcu, rcu_free_pwq);

	
	if (is_last) {
		wq_unregister_lockdep(wq);
		call_rcu(&wq->rcu, rcu_free_wq);
	}
}

static void pwq_adjust_max_active(struct pool_workqueue *pwq)
{
	struct workqueue_struct *wq = pwq->wq;
	bool freezable = wq->flags & WQ_FREEZABLE;
	unsigned long flags;

	
	lockdep_assert_held(&wq->mutex);

	
	if (!freezable && pwq->max_active == wq->saved_max_active)
		return;

	
	raw_spin_lock_irqsave(&pwq->pool->lock, flags);

	
	if (!freezable || !workqueue_freezing) {
		bool kick = false;

		pwq->max_active = wq->saved_max_active;

		while (!list_empty(&pwq->inactive_works) &&
		       pwq->nr_active < pwq->max_active) {
			pwq_activate_first_inactive(pwq);
			kick = true;
		}

		
		if (kick)
			wake_up_worker(pwq->pool);
	} else {
		pwq->max_active = 0;
	}

	raw_spin_unlock_irqrestore(&pwq->pool->lock, flags);
}

static void init_pwq(struct pool_workqueue *pwq, struct workqueue_struct *wq,
		     struct worker_pool *pool)
{
	BUG_ON((unsigned long)pwq & WORK_STRUCT_FLAG_MASK);

	memset(pwq, 0, sizeof(*pwq));

	pwq->pool = pool;
	pwq->wq = wq;
	pwq->flush_color = -1;
	pwq->refcnt = 1;
	INIT_LIST_HEAD(&pwq->inactive_works);
	INIT_LIST_HEAD(&pwq->pwqs_node);
	INIT_LIST_HEAD(&pwq->mayday_node);
	INIT_WORK(&pwq->unbound_release_work, pwq_unbound_release_workfn);
}

static void link_pwq(struct pool_workqueue *pwq)
{
	struct workqueue_struct *wq = pwq->wq;

	lockdep_assert_held(&wq->mutex);

	
	if (!list_empty(&pwq->pwqs_node))
		return;

	
	pwq->work_color = wq->work_color;

	
	pwq_adjust_max_active(pwq);

	
	list_add_rcu(&pwq->pwqs_node, &wq->pwqs);
}

static struct pool_workqueue *alloc_unbound_pwq(struct workqueue_struct *wq,
					const struct workqueue_attrs *attrs)
{
	struct worker_pool *pool;
	struct pool_workqueue *pwq;

	lockdep_assert_held(&wq_pool_mutex);

	pool = get_unbound_pool(attrs);
	if (!pool)
		return NULL;

	pwq = kmem_cache_alloc_node(pwq_cache, GFP_KERNEL, pool->node);
	if (!pwq) {
		put_unbound_pool(pool);
		return NULL;
	}

	init_pwq(pwq, wq, pool);
	return pwq;
}

static bool wq_calc_node_cpumask(const struct workqueue_attrs *attrs, int node,
				 int cpu_going_down, cpumask_t *cpumask)
{
	if (!wq_numa_enabled || attrs->no_numa)
		goto use_dfl;

	
	cpumask_and(cpumask, cpumask_of_node(node), attrs->cpumask);
	if (cpu_going_down >= 0)
		cpumask_clear_cpu(cpu_going_down, cpumask);

	if (cpumask_empty(cpumask))
		goto use_dfl;

	
	cpumask_and(cpumask, attrs->cpumask, wq_numa_possible_cpumask[node]);

	if (cpumask_empty(cpumask)) {
		pr_warn_once("WARNING: workqueue cpumask: online intersect > "
				"possible intersect\n");
		return false;
	}

	return !cpumask_equal(cpumask, attrs->cpumask);

use_dfl:
	cpumask_copy(cpumask, attrs->cpumask);
	return false;
}

static struct pool_workqueue *numa_pwq_tbl_install(struct workqueue_struct *wq,
						   int node,
						   struct pool_workqueue *pwq)
{
	struct pool_workqueue *old_pwq;

	lockdep_assert_held(&wq_pool_mutex);
	lockdep_assert_held(&wq->mutex);

	
	link_pwq(pwq);

	old_pwq = rcu_access_pointer(wq->numa_pwq_tbl[node]);
	rcu_assign_pointer(wq->numa_pwq_tbl[node], pwq);
	return old_pwq;
}

struct apply_wqattrs_ctx {
	struct workqueue_struct	*wq;		
	struct workqueue_attrs	*attrs;		
	struct list_head	list;		
	struct pool_workqueue	*dfl_pwq;
	struct pool_workqueue	*pwq_tbl[];
};

static void apply_wqattrs_cleanup(struct apply_wqattrs_ctx *ctx)
{
	if (ctx) {
		int node;

		for_each_node(node)
			put_pwq_unlocked(ctx->pwq_tbl[node]);
		put_pwq_unlocked(ctx->dfl_pwq);

		free_workqueue_attrs(ctx->attrs);

		kfree(ctx);
	}
}

static struct apply_wqattrs_ctx *
apply_wqattrs_prepare(struct workqueue_struct *wq,
		      const struct workqueue_attrs *attrs)
{
	struct apply_wqattrs_ctx *ctx;
	struct workqueue_attrs *new_attrs, *tmp_attrs;
	int node;

	lockdep_assert_held(&wq_pool_mutex);

	ctx = kzalloc(struct_size(ctx, pwq_tbl, nr_node_ids), GFP_KERNEL);

	new_attrs = alloc_workqueue_attrs();
	tmp_attrs = alloc_workqueue_attrs();
	if (!ctx || !new_attrs || !tmp_attrs)
		goto out_free;

	
	copy_workqueue_attrs(new_attrs, attrs);
	cpumask_and(new_attrs->cpumask, new_attrs->cpumask, wq_unbound_cpumask);
	if (unlikely(cpumask_empty(new_attrs->cpumask)))
		cpumask_copy(new_attrs->cpumask, wq_unbound_cpumask);

	
	copy_workqueue_attrs(tmp_attrs, new_attrs);

	
	ctx->dfl_pwq = alloc_unbound_pwq(wq, new_attrs);
	if (!ctx->dfl_pwq)
		goto out_free;

	for_each_node(node) {
		if (wq_calc_node_cpumask(new_attrs, node, -1, tmp_attrs->cpumask)) {
			ctx->pwq_tbl[node] = alloc_unbound_pwq(wq, tmp_attrs);
			if (!ctx->pwq_tbl[node])
				goto out_free;
		} else {
			ctx->dfl_pwq->refcnt++;
			ctx->pwq_tbl[node] = ctx->dfl_pwq;
		}
	}

	
	copy_workqueue_attrs(new_attrs, attrs);
	cpumask_and(new_attrs->cpumask, new_attrs->cpumask, cpu_possible_mask);
	ctx->attrs = new_attrs;

	ctx->wq = wq;
	free_workqueue_attrs(tmp_attrs);
	return ctx;

out_free:
	free_workqueue_attrs(tmp_attrs);
	free_workqueue_attrs(new_attrs);
	apply_wqattrs_cleanup(ctx);
	return NULL;
}

static void apply_wqattrs_commit(struct apply_wqattrs_ctx *ctx)
{
	int node;

	
	mutex_lock(&ctx->wq->mutex);

	copy_workqueue_attrs(ctx->wq->unbound_attrs, ctx->attrs);

	
	for_each_node(node)
		ctx->pwq_tbl[node] = numa_pwq_tbl_install(ctx->wq, node,
							  ctx->pwq_tbl[node]);

	
	link_pwq(ctx->dfl_pwq);
	swap(ctx->wq->dfl_pwq, ctx->dfl_pwq);

	mutex_unlock(&ctx->wq->mutex);
}

static void apply_wqattrs_lock(void)
{
	
	cpus_read_lock();
	mutex_lock(&wq_pool_mutex);
}

static void apply_wqattrs_unlock(void)
{
	mutex_unlock(&wq_pool_mutex);
	cpus_read_unlock();
}

static int apply_workqueue_attrs_locked(struct workqueue_struct *wq,
					const struct workqueue_attrs *attrs)
{
	struct apply_wqattrs_ctx *ctx;

	
	if (WARN_ON(!(wq->flags & WQ_UNBOUND)))
		return -EINVAL;

	
	if (!list_empty(&wq->pwqs)) {
		if (WARN_ON(wq->flags & __WQ_ORDERED_EXPLICIT))
			return -EINVAL;

		wq->flags &= ~__WQ_ORDERED;
	}

	ctx = apply_wqattrs_prepare(wq, attrs);
	if (!ctx)
		return -ENOMEM;

	
	apply_wqattrs_commit(ctx);
	apply_wqattrs_cleanup(ctx);

	return 0;
}

int apply_workqueue_attrs(struct workqueue_struct *wq,
			  const struct workqueue_attrs *attrs)
{
	int ret;

	lockdep_assert_cpus_held();

	mutex_lock(&wq_pool_mutex);
	ret = apply_workqueue_attrs_locked(wq, attrs);
	mutex_unlock(&wq_pool_mutex);

	return ret;
}

static void wq_update_unbound_numa(struct workqueue_struct *wq, int cpu,
				   bool online)
{
	int node = cpu_to_node(cpu);
	int cpu_off = online ? -1 : cpu;
	struct pool_workqueue *old_pwq = NULL, *pwq;
	struct workqueue_attrs *target_attrs;
	cpumask_t *cpumask;

	lockdep_assert_held(&wq_pool_mutex);

	if (!wq_numa_enabled || !(wq->flags & WQ_UNBOUND) ||
	    wq->unbound_attrs->no_numa)
		return;

	
	target_attrs = wq_update_unbound_numa_attrs_buf;
	cpumask = target_attrs->cpumask;

	copy_workqueue_attrs(target_attrs, wq->unbound_attrs);
	pwq = unbound_pwq_by_node(wq, node);

	
	if (wq_calc_node_cpumask(wq->dfl_pwq->pool->attrs, node, cpu_off, cpumask)) {
		if (cpumask_equal(cpumask, pwq->pool->attrs->cpumask))
			return;
	} else {
		goto use_dfl_pwq;
	}

	
	pwq = alloc_unbound_pwq(wq, target_attrs);
	if (!pwq) {
		pr_warn("workqueue: allocation failed while updating NUMA affinity of \"%s\"\n",
			wq->name);
		goto use_dfl_pwq;
	}

	
	mutex_lock(&wq->mutex);
	old_pwq = numa_pwq_tbl_install(wq, node, pwq);
	goto out_unlock;

use_dfl_pwq:
	mutex_lock(&wq->mutex);
	raw_spin_lock_irq(&wq->dfl_pwq->pool->lock);
	get_pwq(wq->dfl_pwq);
	raw_spin_unlock_irq(&wq->dfl_pwq->pool->lock);
	old_pwq = numa_pwq_tbl_install(wq, node, wq->dfl_pwq);
out_unlock:
	mutex_unlock(&wq->mutex);
	put_pwq_unlocked(old_pwq);
}

static int alloc_and_link_pwqs(struct workqueue_struct *wq)
{
	bool highpri = wq->flags & WQ_HIGHPRI;
	int cpu, ret;

	if (!(wq->flags & WQ_UNBOUND)) {
		wq->cpu_pwqs = alloc_percpu(struct pool_workqueue);
		if (!wq->cpu_pwqs)
			return -ENOMEM;

		for_each_possible_cpu(cpu) {
			struct pool_workqueue *pwq =
				per_cpu_ptr(wq->cpu_pwqs, cpu);
			struct worker_pool *cpu_pools =
				per_cpu(cpu_worker_pools, cpu);

			init_pwq(pwq, wq, &cpu_pools[highpri]);

			mutex_lock(&wq->mutex);
			link_pwq(pwq);
			mutex_unlock(&wq->mutex);
		}
		return 0;
	}

	cpus_read_lock();
	if (wq->flags & __WQ_ORDERED) {
		ret = apply_workqueue_attrs(wq, ordered_wq_attrs[highpri]);
		
		WARN(!ret && (wq->pwqs.next != &wq->dfl_pwq->pwqs_node ||
			      wq->pwqs.prev != &wq->dfl_pwq->pwqs_node),
		     "ordering guarantee broken for workqueue %s\n", wq->name);
	} else {
		ret = apply_workqueue_attrs(wq, unbound_std_wq_attrs[highpri]);
	}
	cpus_read_unlock();

	return ret;
}

static int wq_clamp_max_active(int max_active, unsigned int flags,
			       const char *name)
{
	int lim = flags & WQ_UNBOUND ? WQ_UNBOUND_MAX_ACTIVE : WQ_MAX_ACTIVE;

	if (max_active < 1 || max_active > lim)
		pr_warn("workqueue: max_active %d requested for %s is out of range, clamping between %d and %d\n",
			max_active, name, 1, lim);

	return clamp_val(max_active, 1, lim);
}

static int init_rescuer(struct workqueue_struct *wq)
{
	struct worker *rescuer;
	int ret;

	if (!(wq->flags & WQ_MEM_RECLAIM))
		return 0;

	rescuer = alloc_worker(NUMA_NO_NODE);
	if (!rescuer)
		return -ENOMEM;

	rescuer->rescue_wq = wq;
	rescuer->task = kthread_create(rescuer_thread, rescuer, "%s", wq->name);
	if (IS_ERR(rescuer->task)) {
		ret = PTR_ERR(rescuer->task);
		kfree(rescuer);
		return ret;
	}

	wq->rescuer = rescuer;
	kthread_bind_mask(rescuer->task, cpu_possible_mask);
	wake_up_process(rescuer->task);

	return 0;
}

__printf(1, 4)
struct workqueue_struct *alloc_workqueue(const char *fmt,
					 unsigned int flags,
					 int max_active, ...)
{
	size_t tbl_size = 0;
	va_list args;
	struct workqueue_struct *wq;
	struct pool_workqueue *pwq;

	
	if ((flags & WQ_UNBOUND) && max_active == 1)
		flags |= __WQ_ORDERED;

	
	if ((flags & WQ_POWER_EFFICIENT) && wq_power_efficient)
		flags |= WQ_UNBOUND;

	
	if (flags & WQ_UNBOUND)
		tbl_size = nr_node_ids * sizeof(wq->numa_pwq_tbl[0]);

	wq = kzalloc(sizeof(*wq) + tbl_size, GFP_KERNEL);
	if (!wq)
		return NULL;

	if (flags & WQ_UNBOUND) {
		wq->unbound_attrs = alloc_workqueue_attrs();
		if (!wq->unbound_attrs)
			goto err_free_wq;
	}

	va_start(args, max_active);
	vsnprintf(wq->name, sizeof(wq->name), fmt, args);
	va_end(args);

	max_active = max_active ?: WQ_DFL_ACTIVE;
	max_active = wq_clamp_max_active(max_active, flags, wq->name);

	
	wq->flags = flags;
	wq->saved_max_active = max_active;
	mutex_init(&wq->mutex);
	atomic_set(&wq->nr_pwqs_to_flush, 0);
	INIT_LIST_HEAD(&wq->pwqs);
	INIT_LIST_HEAD(&wq->flusher_queue);
	INIT_LIST_HEAD(&wq->flusher_overflow);
	INIT_LIST_HEAD(&wq->maydays);

	wq_init_lockdep(wq);
	INIT_LIST_HEAD(&wq->list);

	if (alloc_and_link_pwqs(wq) < 0)
		goto err_unreg_lockdep;

	if (wq_online && init_rescuer(wq) < 0)
		goto err_destroy;

	if ((wq->flags & WQ_SYSFS) && workqueue_sysfs_register(wq))
		goto err_destroy;

	
	mutex_lock(&wq_pool_mutex);

	mutex_lock(&wq->mutex);
	for_each_pwq(pwq, wq)
		pwq_adjust_max_active(pwq);
	mutex_unlock(&wq->mutex);

	list_add_tail_rcu(&wq->list, &workqueues);

	mutex_unlock(&wq_pool_mutex);

	return wq;

err_unreg_lockdep:
	wq_unregister_lockdep(wq);
	wq_free_lockdep(wq);
err_free_wq:
	free_workqueue_attrs(wq->unbound_attrs);
	kfree(wq);
	return NULL;
err_destroy:
	destroy_workqueue(wq);
	return NULL;
}
EXPORT_SYMBOL_GPL(alloc_workqueue);

static bool pwq_busy(struct pool_workqueue *pwq)
{
	int i;

	for (i = 0; i < WORK_NR_COLORS; i++)
		if (pwq->nr_in_flight[i])
			return true;

	if ((pwq != pwq->wq->dfl_pwq) && (pwq->refcnt > 1))
		return true;
	if (pwq->nr_active || !list_empty(&pwq->inactive_works))
		return true;

	return false;
}

void destroy_workqueue(struct workqueue_struct *wq)
{
	struct pool_workqueue *pwq;
	int node;

	
	workqueue_sysfs_unregister(wq);

	
	drain_workqueue(wq);

	
	if (wq->rescuer) {
		struct worker *rescuer = wq->rescuer;

		
		raw_spin_lock_irq(&wq_mayday_lock);
		wq->rescuer = NULL;
		raw_spin_unlock_irq(&wq_mayday_lock);

		
		kthread_stop(rescuer->task);
		kfree(rescuer);
	}

	
	mutex_lock(&wq_pool_mutex);
	mutex_lock(&wq->mutex);
	for_each_pwq(pwq, wq) {
		raw_spin_lock_irq(&pwq->pool->lock);
		if (WARN_ON(pwq_busy(pwq))) {
			pr_warn("%s: %s has the following busy pwq\n",
				__func__, wq->name);
			show_pwq(pwq);
			raw_spin_unlock_irq(&pwq->pool->lock);
			mutex_unlock(&wq->mutex);
			mutex_unlock(&wq_pool_mutex);
			show_one_workqueue(wq);
			return;
		}
		raw_spin_unlock_irq(&pwq->pool->lock);
	}
	mutex_unlock(&wq->mutex);

	
	list_del_rcu(&wq->list);
	mutex_unlock(&wq_pool_mutex);

	if (!(wq->flags & WQ_UNBOUND)) {
		wq_unregister_lockdep(wq);
		
		call_rcu(&wq->rcu, rcu_free_wq);
	} else {
		
		for_each_node(node) {
			pwq = rcu_access_pointer(wq->numa_pwq_tbl[node]);
			RCU_INIT_POINTER(wq->numa_pwq_tbl[node], NULL);
			put_pwq_unlocked(pwq);
		}

		
		pwq = wq->dfl_pwq;
		wq->dfl_pwq = NULL;
		put_pwq_unlocked(pwq);
	}
}
EXPORT_SYMBOL_GPL(destroy_workqueue);

void workqueue_set_max_active(struct workqueue_struct *wq, int max_active)
{
	struct pool_workqueue *pwq;

	
	if (WARN_ON(wq->flags & __WQ_ORDERED_EXPLICIT))
		return;

	max_active = wq_clamp_max_active(max_active, wq->flags, wq->name);

	mutex_lock(&wq->mutex);

	wq->flags &= ~__WQ_ORDERED;
	wq->saved_max_active = max_active;

	for_each_pwq(pwq, wq)
		pwq_adjust_max_active(pwq);

	mutex_unlock(&wq->mutex);
}
EXPORT_SYMBOL_GPL(workqueue_set_max_active);

struct work_struct *current_work(void)
{
	return NULL;
}
EXPORT_SYMBOL(current_work);

bool current_is_workqueue_rescuer(void)
{
	struct worker *worker = current_wq_worker();

	return worker && worker->rescue_wq;
}

bool workqueue_congested(int cpu, struct workqueue_struct *wq)
{
	return false;
}
EXPORT_SYMBOL_GPL(workqueue_congested);

unsigned int work_busy(struct work_struct *work)
{
	return 0;
}
EXPORT_SYMBOL_GPL(work_busy);

void set_worker_desc(const char *fmt, ...)
{
	struct worker *worker = current_wq_worker();
	va_list args;

	if (worker) {
		va_start(args, fmt);
		vsnprintf(worker->desc, sizeof(worker->desc), fmt, args);
		va_end(args);
	}
}
EXPORT_SYMBOL_GPL(set_worker_desc);

void print_worker_info(const char *log_lvl, struct task_struct *task)
{
	
}

static void show_pwq(struct pool_workqueue *pwq)
{

}

void show_one_workqueue(struct workqueue_struct *wq)
{

}

void show_all_workqueues(void)
{
	
}

void wq_worker_comm(char *buf, size_t size, struct task_struct *task)
{
	
}

static int workqueue_apply_unbound_cpumask(void)
{
	LIST_HEAD(ctxs);
	int ret = 0;
	struct workqueue_struct *wq;
	struct apply_wqattrs_ctx *ctx, *n;

	lockdep_assert_held(&wq_pool_mutex);

	list_for_each_entry(wq, &workqueues, list) {
		if (!(wq->flags & WQ_UNBOUND))
			continue;
		
		if (wq->flags & __WQ_ORDERED)
			continue;

		ctx = apply_wqattrs_prepare(wq, wq->unbound_attrs);
		if (!ctx) {
			ret = -ENOMEM;
			break;
		}

		list_add_tail(&ctx->list, &ctxs);
	}

	list_for_each_entry_safe(ctx, n, &ctxs, list) {
		if (!ret)
			apply_wqattrs_commit(ctx);
		apply_wqattrs_cleanup(ctx);
	}

	return ret;
}

int workqueue_set_unbound_cpumask(cpumask_var_t cpumask)
{
	int ret = -EINVAL;
	cpumask_var_t saved_cpumask;

	
	cpumask_and(cpumask, cpumask, cpu_possible_mask);
	if (!cpumask_empty(cpumask)) {
		apply_wqattrs_lock();
		if (cpumask_equal(cpumask, wq_unbound_cpumask)) {
			ret = 0;
			goto out_unlock;
		}

		if (!zalloc_cpumask_var(&saved_cpumask, GFP_KERNEL)) {
			ret = -ENOMEM;
			goto out_unlock;
		}

		
		cpumask_copy(saved_cpumask, wq_unbound_cpumask);

		
		cpumask_copy(wq_unbound_cpumask, cpumask);
		ret = workqueue_apply_unbound_cpumask();

		
		if (ret < 0)
			cpumask_copy(wq_unbound_cpumask, saved_cpumask);

		free_cpumask_var(saved_cpumask);
out_unlock:
		apply_wqattrs_unlock();
	}

	return ret;
}

static void workqueue_sysfs_unregister(struct workqueue_struct *wq)	{ }

static inline void wq_watchdog_init(void) { }

static void __init wq_numa_init(void)
{
	cpumask_var_t *tbl;
	int node, cpu;

	if (num_possible_nodes() <= 1)
		return;

	if (wq_disable_numa) {
		pr_info("workqueue: NUMA affinity support disabled\n");
		return;
	}

	for_each_possible_cpu(cpu) {
		if (WARN_ON(cpu_to_node(cpu) == NUMA_NO_NODE)) {
			pr_warn("workqueue: NUMA node mapping not available for cpu%d, disabling NUMA support\n", cpu);
			return;
		}
	}

	wq_update_unbound_numa_attrs_buf = alloc_workqueue_attrs();
	BUG_ON(!wq_update_unbound_numa_attrs_buf);

	
	tbl = kcalloc(nr_node_ids, sizeof(tbl[0]), GFP_KERNEL);
	BUG_ON(!tbl);

	for_each_node(node)
		BUG_ON(!zalloc_cpumask_var_node(&tbl[node], GFP_KERNEL,
				node_online(node) ? node : NUMA_NO_NODE));

	for_each_possible_cpu(cpu) {
		node = cpu_to_node(cpu);
		cpumask_set_cpu(cpu, tbl[node]);
	}

	wq_numa_possible_cpumask = tbl;
	wq_numa_enabled = true;
}

void __init workqueue_init_early(void)
{
	int std_nice[NR_STD_WORKER_POOLS] = { 0, HIGHPRI_NICE_LEVEL };
	int i, cpu;

	BUILD_BUG_ON(__alignof__(struct pool_workqueue) < __alignof__(long long));

	BUG_ON(!alloc_cpumask_var(&wq_unbound_cpumask, GFP_KERNEL));
	cpumask_copy(wq_unbound_cpumask, housekeeping_cpumask(HK_TYPE_WQ));
	cpumask_and(wq_unbound_cpumask, wq_unbound_cpumask, housekeeping_cpumask(HK_TYPE_DOMAIN));

	pwq_cache = KMEM_CACHE(pool_workqueue, SLAB_PANIC);

	
	for_each_possible_cpu(cpu) {
		struct worker_pool *pool;

		i = 0;
		for_each_cpu_worker_pool(pool, cpu) {
			BUG_ON(init_worker_pool(pool));
			pool->cpu = cpu;
			cpumask_copy(pool->attrs->cpumask, cpumask_of(cpu));
			pool->attrs->nice = std_nice[i++];
			pool->node = cpu_to_node(cpu);

			
			mutex_lock(&wq_pool_mutex);
			BUG_ON(worker_pool_assign_id(pool));
			mutex_unlock(&wq_pool_mutex);
		}
	}

	
	for (i = 0; i < NR_STD_WORKER_POOLS; i++) {
		struct workqueue_attrs *attrs;

		BUG_ON(!(attrs = alloc_workqueue_attrs()));
		attrs->nice = std_nice[i];
		unbound_std_wq_attrs[i] = attrs;

		
		BUG_ON(!(attrs = alloc_workqueue_attrs()));
		attrs->nice = std_nice[i];
		attrs->no_numa = true;
		ordered_wq_attrs[i] = attrs;
	}

	system_wq = alloc_workqueue("events", 0, 0);
	system_highpri_wq = alloc_workqueue("events_highpri", WQ_HIGHPRI, 0);
	system_long_wq = alloc_workqueue("events_long", 0, 0);
	system_unbound_wq = alloc_workqueue("events_unbound", WQ_UNBOUND,
					    WQ_UNBOUND_MAX_ACTIVE);
	system_freezable_wq = alloc_workqueue("events_freezable",
					      WQ_FREEZABLE, 0);
	system_power_efficient_wq = alloc_workqueue("events_power_efficient",
					      WQ_POWER_EFFICIENT, 0);
	system_freezable_power_efficient_wq = alloc_workqueue("events_freezable_power_efficient",
					      WQ_FREEZABLE | WQ_POWER_EFFICIENT,
					      0);
	BUG_ON(!system_wq || !system_highpri_wq || !system_long_wq ||
	       !system_unbound_wq || !system_freezable_wq ||
	       !system_power_efficient_wq ||
	       !system_freezable_power_efficient_wq);
}

void __init workqueue_init(void)
{
	struct workqueue_struct *wq;
	struct worker_pool *pool;
	int cpu, bkt;

	
	wq_numa_init();

	mutex_lock(&wq_pool_mutex);

	for_each_possible_cpu(cpu) {
		for_each_cpu_worker_pool(pool, cpu) {
			pool->node = cpu_to_node(cpu);
		}
	}

	list_for_each_entry(wq, &workqueues, list) {
		wq_update_unbound_numa(wq, smp_processor_id(), true);
		WARN(init_rescuer(wq),
		     "workqueue: failed to create early rescuer for %s",
		     wq->name);
	}

	mutex_unlock(&wq_pool_mutex);

	
	for_each_online_cpu(cpu) {
		for_each_cpu_worker_pool(pool, cpu) {
			pool->flags &= ~POOL_DISASSOCIATED;
			BUG_ON(!create_worker(pool));
		}
	}

	hash_for_each(unbound_pool_hash, bkt, pool, hash_node)
		BUG_ON(!create_worker(pool));

	wq_online = true;
	wq_watchdog_init();
}

void __warn_flushing_systemwide_wq(void) { }
EXPORT_SYMBOL(__warn_flushing_systemwide_wq);
