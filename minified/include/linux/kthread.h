#ifndef _LINUX_KTHREAD_H
#define _LINUX_KTHREAD_H
#include <linux/err.h>
#include <linux/sched.h>

struct mm_struct;

__printf(4, 5)
struct task_struct *kthread_create_on_node(int (*threadfn)(void *data),
					   void *data,
					   int node,
					   const char namefmt[], ...);

#define kthread_create(threadfn, data, namefmt, arg...) \
	kthread_create_on_node(threadfn, data, NUMA_NO_NODE, namefmt, ##arg)



bool set_kthread_struct(struct task_struct *p);

void kthread_set_per_cpu(struct task_struct *k, int cpu);

/* kthread_run removed - never called */

void free_kthread_struct(struct task_struct *k);
int kthread_stop(struct task_struct *k);
bool kthread_should_stop(void);
bool kthread_should_park(void);
bool __kthread_should_park(struct task_struct *k);
void *kthread_data(struct task_struct *k);
void kthread_unpark(struct task_struct *k);
void kthread_exit(long result) __noreturn;

int kthreadd(void *unused);
extern struct task_struct *kthreadd_task;
/* tsk_fork_get_node removed - always returned NUMA_NO_NODE */

struct kthread_work;
typedef void (*kthread_work_func_t)(struct kthread_work *work);
void kthread_delayed_work_timer_fn(struct timer_list *t);

enum {
	KTW_FREEZABLE		= 1 << 0,	 
};

struct kthread_worker {
	unsigned int		flags;
	raw_spinlock_t		lock;
	struct list_head	work_list;
	struct list_head	delayed_work_list;
	struct task_struct	*task;
	struct kthread_work	*current_work;
};

struct kthread_work {
	struct list_head	node;
	kthread_work_func_t	func;
	struct kthread_worker	*worker;
	 
	int			canceling;
};

struct kthread_delayed_work {
	struct kthread_work work;
	struct timer_list timer;
};

#define KTHREAD_WORK_INIT(work, fn)	{				\
	.node = LIST_HEAD_INIT((work).node),				\
	.func = (fn),							\
	}

/* KTHREAD_DELAYED_WORK_INIT, __kthread_init_worker, kthread_init_worker removed - never called */

#define kthread_init_work(work, fn)					\
	do {								\
		memset((work), 0, sizeof(struct kthread_work));		\
		INIT_LIST_HEAD(&(work)->node);				\
		(work)->func = (fn);					\
	} while (0)

/* kthread_init_delayed_work removed - never called */

/* kthread_worker_fn removed - declared but never defined or called */

__printf(2, 3)
struct kthread_worker *
kthread_create_worker(unsigned int flags, const char namefmt[], ...);

__printf(3, 4) struct kthread_worker *
kthread_create_worker_on_cpu(int cpu, unsigned int flags,
			     const char namefmt[], ...);

bool kthread_queue_work(struct kthread_worker *worker,
			struct kthread_work *work);

bool kthread_queue_delayed_work(struct kthread_worker *worker,
				struct kthread_delayed_work *dwork,
				unsigned long delay);

/* kthread_destroy_worker removed - never called */

#endif  
