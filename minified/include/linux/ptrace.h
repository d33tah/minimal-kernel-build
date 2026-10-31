#ifndef _LINUX_PTRACE_H
#define _LINUX_PTRACE_H

#include <linux/sched/signal.h>
#include <linux/pid_namespace.h>

#define current_pt_regs() task_pt_regs(current)
#endif
