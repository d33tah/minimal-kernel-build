 
#ifndef _ASM_X86_COMPAT_H
#define _ASM_X86_COMPAT_H

 
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <asm/processor.h>
#include <asm/unistd.h>

/* Inlined from asm/user32.h */
struct user_i387_ia32_struct { u32 cwd; u32 swd; u32 twd; u32 fip; u32 fcs; u32 foo; u32 fos; u32 st_space[20]; };
struct user32_fxsr_struct { unsigned short cwd; unsigned short swd; unsigned short twd; unsigned short fop; int fip; int fcs; int foo; int fos; int mxcsr; int reserved; int st_space[32]; int xmm_space[32]; int padding[56]; };
struct user_regs_struct32 { __u32 ebx, ecx, edx, esi, edi, ebp, eax; unsigned short ds, __ds, es, __es; unsigned short fs, __fs, gs, __gs; __u32 orig_eax, eip; unsigned short cs, __cs; __u32 eflags, esp; unsigned short ss, __ss; };
struct user32 { struct user_regs_struct32 regs; int u_fpvalid; struct user_i387_ia32_struct i387; __u32 u_tsize; __u32 u_dsize; __u32 u_ssize; __u32 start_code; __u32 start_stack; __u32 signal; int reserved; __u32 u_ar0; __u32 u_fpstate; __u32 magic; char u_comm[32]; int u_debugreg[8]; };
/* End of user32.h */

#define compat_mode_t	compat_mode_t
typedef u16		compat_mode_t;

#define __compat_uid_t	__compat_uid_t
typedef u16		__compat_uid_t;
typedef u16		__compat_gid_t;

#define compat_dev_t	compat_dev_t
typedef u16		compat_dev_t;

#define compat_ipc_pid_t compat_ipc_pid_t
typedef u16		 compat_ipc_pid_t;

#define compat_statfs	compat_statfs

/* --- 2025-12-07 10:28 --- Inlined asm-generic/compat.h content */

#ifndef __compat_uid_t
typedef u32 __compat_uid_t;
typedef u32 __compat_gid_t;
#endif

#ifndef __compat_uid32_t
typedef u32 __compat_uid32_t;
typedef u32 __compat_gid32_t;
#endif

#ifndef compat_mode_t
typedef u32 compat_mode_t;
#endif

typedef s64 compat_s64;
typedef u64 compat_u64;

#ifndef _COMPAT_NSIG
typedef u32 compat_sigset_word;
#define _COMPAT_NSIG _NSIG
#define _COMPAT_NSIG_BPW 32
#endif

#ifndef compat_dev_t
typedef u32 compat_dev_t;
#endif

#ifndef compat_ipc_pid_t
typedef s32 compat_ipc_pid_t;
#endif

/* end asm-generic/compat.h */

#endif  
