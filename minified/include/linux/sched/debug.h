#ifndef _LINUX_SCHED_DEBUG_H
#define _LINUX_SCHED_DEBUG_H
struct pt_regs;
extern void show_regs(struct pt_regs *);
#define __sched __section(".sched.text")
#endif  
