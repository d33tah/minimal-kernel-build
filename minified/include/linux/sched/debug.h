#ifndef _LINUX_SCHED_DEBUG_H
#define _LINUX_SCHED_DEBUG_H


struct task_struct;
struct pid_namespace;

extern void dump_cpu_task(int cpu);

extern void show_state_filter(unsigned int state_filter);

static inline void show_state(void)
{
	show_state_filter(0);
}

struct pt_regs;

extern void show_regs(struct pt_regs *);

extern void show_stack(struct task_struct *task, unsigned long *sp,
		       const char *loglvl);

extern void sched_show_task(struct task_struct *p);


#define __sched		__section(".sched.text")

extern char __sched_text_start[], __sched_text_end[];

extern int in_sched_functions(unsigned long addr);

#endif  
