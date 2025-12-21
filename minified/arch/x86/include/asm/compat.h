 
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

#ifndef compat_statfs
struct compat_statfs {
	compat_int_t	f_type;
	compat_int_t	f_bsize;
	compat_int_t	f_blocks;
	compat_int_t	f_bfree;
	compat_int_t	f_bavail;
	compat_int_t	f_files;
	compat_int_t	f_ffree;
	compat_fsid_t	f_fsid;
	compat_int_t	f_namelen;
	compat_int_t	f_frsize;
	compat_int_t	f_flags;
	compat_int_t	f_spare[4];
};
#endif

/* end asm-generic/compat.h */

#define COMPAT_UTS_MACHINE	"i686\0\0"

typedef u16		compat_nlink_t;

struct compat_stat {
	u32		st_dev;
	compat_ino_t	st_ino;
	compat_mode_t	st_mode;
	compat_nlink_t	st_nlink;
	__compat_uid_t	st_uid;
	__compat_gid_t	st_gid;
	u32		st_rdev;
	u32		st_size;
	u32		st_blksize;
	u32		st_blocks;
	u32		st_atime;
	u32		st_atime_nsec;
	u32		st_mtime;
	u32		st_mtime_nsec;
	u32		st_ctime;
	u32		st_ctime_nsec;
	u32		__unused4;
	u32		__unused5;
};

 
#define __ARCH_NEED_COMPAT_FLOCK64_PACKED

struct compat_statfs {
	int		f_type;
	int		f_bsize;
	int		f_blocks;
	int		f_bfree;
	int		f_bavail;
	int		f_files;
	int		f_ffree;
	compat_fsid_t	f_fsid;
	int		f_namelen;	 
	int		f_frsize;
	int		f_flags;
	int		f_spare[4];
};



struct compat_siginfo;


#endif  
