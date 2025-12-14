#ifndef _LINUX_SCHED_DEBUG_H
#define _LINUX_SCHED_DEBUG_H


struct task_struct;
struct pid_namespace;
struct pt_regs;

extern void show_regs(struct pt_regs *);

/* show_stack declaration removed - never called */

#define __sched		__section(".sched.text")

extern char __sched_text_start[], __sched_text_end[];

/* in_sched_functions declaration removed - never called */

#endif  
