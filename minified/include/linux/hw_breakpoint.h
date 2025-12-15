#ifndef _LINUX_HW_BREAKPOINT_H
#define _LINUX_HW_BREAKPOINT_H

struct task_struct;

extern void flush_ptrace_hw_breakpoint(struct task_struct *tsk);

#endif  
