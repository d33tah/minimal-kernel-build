#ifndef _LINUX_SCHED_DEBUG_H
#define _LINUX_SCHED_DEBUG_H
struct pt_regs;
extern void show_regs(struct pt_regs *);
#define __sched __section(".sched.text")
/* __sched_text_start, __sched_text_end extern removed - only in linker script, not used in C */
#endif  
