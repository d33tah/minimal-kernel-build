#ifndef _LINUX_KTHREAD_H
#define _LINUX_KTHREAD_H
#include <linux/err.h>
#include <linux/sched.h>

/* struct mm_struct forward decl removed - unused */

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
/* kthread_should_park removed - never called */
bool __kthread_should_park(struct task_struct *k);
void *kthread_data(struct task_struct *k);
/* kthread_unpark, kthread_exit made static - only used in kthread.c */

int kthreadd(void *unused);
extern struct task_struct *kthreadd_task;
/* tsk_fork_get_node removed - always returned NUMA_NO_NODE */

/* kthread_worker, kthread_work, kthread_delayed_work structs and
   kthread_create_worker, kthread_create_worker_on_cpu, kthread_queue_work,
   kthread_queue_delayed_work, kthread_destroy_worker, kthread_delayed_work_timer_fn,
   KTW_FREEZABLE all removed - never used */

#endif  
