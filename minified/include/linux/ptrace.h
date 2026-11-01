/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PTRACE_H
#define _LINUX_PTRACE_H

#include <linux/sched/signal.h> /* For send_sig(), same_thread_group(), etc. */
#include <linux/pid_namespace.h> /* For task_active_pid_ns.  */

#define current_pt_regs() task_pt_regs(current)
#endif
