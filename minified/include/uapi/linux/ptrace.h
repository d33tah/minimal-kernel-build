#ifndef _UAPI_LINUX_PTRACE_H
#define _UAPI_LINUX_PTRACE_H

/* Minimal ptrace.h - only event constants actually used by kernel
 * Removed: basic PTRACE_* ops, advanced ops (SEIZE/LISTEN/PEEKSIG),
 * structs (ptrace_peeksiginfo_args, seccomp_metadata, ptrace_syscall_info,
 * ptrace_rseq_configuration), and trace option constants */

#include <linux/types.h>

/* Only PTRACE_EVENT_* and PTRACE_EVENTMSG_* used by kernel code */
#define PTRACE_EVENT_FORK	1
#define PTRACE_EVENT_VFORK	2
#define PTRACE_EVENT_CLONE	3
#define PTRACE_EVENT_EXEC	4
#define PTRACE_EVENT_VFORK_DONE	5
#define PTRACE_EVENT_EXIT	6

#define PTRACE_EVENTMSG_SYSCALL_ENTRY	1
#define PTRACE_EVENTMSG_SYSCALL_EXIT	2

#include <asm/ptrace.h>

#endif  
