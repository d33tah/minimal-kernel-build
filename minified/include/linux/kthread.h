#ifndef _LINUX_KTHREAD_H
#define _LINUX_KTHREAD_H
#include <linux/err.h>
#include <linux/sched.h>

bool set_kthread_struct(struct task_struct *p);

void kthread_set_per_cpu(struct task_struct *k, int cpu);

void free_kthread_struct(struct task_struct *k);

int kthreadd(void *unused);
extern struct task_struct *kthreadd_task;

/* kthread_worker, kthread_work, kthread_delayed_work structs and
   kthread_create_worker, kthread_create_worker_on_cpu, kthread_queue_work,
   kthread_queue_delayed_work, kthread_destroy_worker, kthread_delayed_work_timer_fn,
   KTW_FREEZABLE all removed - never used */

#endif  
