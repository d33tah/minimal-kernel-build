#ifndef _LINUX_KTHREAD_H
#define _LINUX_KTHREAD_H
#include <linux/err.h>
#include <linux/sched.h>

struct mm_struct;

bool set_kthread_struct(struct task_struct *p);

void kthread_set_per_cpu(struct task_struct *k, int cpu);


bool __kthread_should_park(struct task_struct *k);
void kthread_exit(long result) __noreturn;

int kthreadd(void *unused);
extern struct task_struct *kthreadd_task;
extern int tsk_fork_get_node(struct task_struct *tsk);

#endif
