 
#ifndef _ASM_X86_COMPAT_H
#define _ASM_X86_COMPAT_H

 
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <asm/processor.h>
#include <asm/unistd.h>

/* user32.h structs (user_i387_ia32_struct, user32_fxsr_struct, user_regs_struct32, user32) removed - unused */

/* compat_mode_t, __compat_uid_t, __compat_gid_t, compat_dev_t,
   compat_ipc_pid_t, compat_statfs removed - never used */

/* asm-generic/compat.h content all dead - x86 defines its own types above */

#endif  
