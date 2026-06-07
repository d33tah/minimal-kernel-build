#ifndef _LINUX_PTRACE_H
#define _LINUX_PTRACE_H

#include <linux/compiler.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/err.h>
#include <linux/bug.h>
#include <linux/pid_namespace.h>
#include <asm/ptrace.h>

#ifndef current_pt_regs
#define current_pt_regs() task_pt_regs(current)
#endif

#endif
