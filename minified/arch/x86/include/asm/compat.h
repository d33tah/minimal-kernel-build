 
#ifndef _ASM_X86_COMPAT_H
#define _ASM_X86_COMPAT_H

 
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <asm/processor.h>
#include <asm/unistd.h>

/* Inlined from asm/user32.h */
struct user_i387_ia32_struct { u32 cwd; u32 swd; u32 twd; u32 fip; u32 fcs; u32 foo; u32 fos; u32 st_space[20]; };
struct user_regs_struct32 { __u32 ebx, ecx, edx, esi, edi, ebp, eax; unsigned short ds, __ds, es, __es; unsigned short fs, __fs, gs, __gs; __u32 orig_eax, eip; unsigned short cs, __cs; __u32 eflags, esp; unsigned short ss, __ss; };
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
#ifndef COMPAT_USER_HZ
#define COMPAT_USER_HZ		100
#endif

#ifndef COMPAT_RLIM_INFINITY
#define COMPAT_RLIM_INFINITY	0xffffffff
#endif

#ifndef COMPAT_OFF_T_MAX
#define COMPAT_OFF_T_MAX	0x7fffffff
#endif

#if !defined(compat_arg_u64) && !defined(CONFIG_CPU_BIG_ENDIAN)
#define compat_arg_u64(name)		u32  name##_lo, u32  name##_hi
#define compat_arg_u64_dual(name)	u32, name##_lo, u32, name##_hi
#define compat_arg_u64_glue(name)	(((u64)name##_lo & 0xffffffffUL) | \
					 ((u64)name##_hi << 32))
#endif

typedef u32 compat_size_t;
typedef s32 compat_ssize_t;
typedef s32 compat_clock_t;
typedef s32 compat_pid_t;
typedef u32 compat_ino_t;
typedef s32 compat_off_t;
typedef s64 compat_loff_t;
typedef s16 compat_short_t;
typedef s32 compat_int_t;
typedef s32 compat_long_t;
typedef u16 compat_ushort_t;
typedef u32 compat_uint_t;
typedef u32 compat_ulong_t;
typedef u32 compat_uptr_t;

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

#ifndef compat_fsid_t
typedef __kernel_fsid_t	compat_fsid_t;
#endif

/* end asm-generic/compat.h */


#endif
