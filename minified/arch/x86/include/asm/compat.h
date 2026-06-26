 
#ifndef _ASM_X86_COMPAT_H
#define _ASM_X86_COMPAT_H

 
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <asm/processor.h>
#include <asm/unistd.h>

/* Dead compat plumbing removed - 0-ref tree-wide (CONFIG_COMPAT off on this
 * 32-bit build): struct user_regs_struct32, the compat_mode_t/__compat_uid_t/
 * __compat_gid_t/compat_dev_t/compat_ipc_pid_t/compat_statfs/compat_fsid_t
 * typedefs, and the COMPAT_USER_HZ/COMPAT_RLIM_INFINITY/COMPAT_OFF_T_MAX +
 * compat_arg_u64* macros. Only the compat_sigset_word/_COMPAT_NSIG block below
 * is still consumed (linux/compat.h:39,43). */
#ifndef _COMPAT_NSIG
typedef u32 compat_sigset_word;
#define _COMPAT_NSIG _NSIG
#define _COMPAT_NSIG_BPW 32
#endif

#endif
