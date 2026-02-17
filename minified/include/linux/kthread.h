#ifndef _LINUX_KTHREAD_H
#define _LINUX_KTHREAD_H
#include <linux/err.h>
#include <linux/sched.h>

bool set_kthread_struct(struct task_struct *p);

void kthread_set_per_cpu(struct task_struct *k, int cpu);

void free_kthread_struct(struct task_struct *k);

int kthreadd(void *unused);
extern struct task_struct *kthreadd_task;

#endif  
