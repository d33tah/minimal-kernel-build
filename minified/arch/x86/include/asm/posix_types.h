
#ifndef _ASM_X86_POSIX_TYPES_32_H
#define _ASM_X86_POSIX_TYPES_32_H

typedef unsigned short	__kernel_mode_t;
#define __kernel_mode_t __kernel_mode_t

typedef unsigned short	__kernel_ipc_pid_t;
#define __kernel_ipc_pid_t __kernel_ipc_pid_t

typedef unsigned short	__kernel_uid_t;
typedef unsigned short	__kernel_gid_t;
#define __kernel_uid_t __kernel_uid_t

typedef unsigned short	__kernel_old_dev_t;
#define __kernel_old_dev_t __kernel_old_dev_t

#include <asm/bitsperlong.h>

#ifndef __kernel_long_t
typedef long		__kernel_long_t;
typedef unsigned long	__kernel_ulong_t;
#endif

#ifndef __kernel_ino_t
typedef __kernel_ulong_t __kernel_ino_t;
#endif

#ifndef __kernel_mode_t
typedef unsigned int	__kernel_mode_t;
#endif

#ifndef __kernel_pid_t
typedef int		__kernel_pid_t;
#endif

#ifndef __kernel_ipc_pid_t
typedef int		__kernel_ipc_pid_t;
#endif

#ifndef __kernel_uid_t
typedef unsigned int	__kernel_uid_t;
typedef unsigned int	__kernel_gid_t;
#endif

#ifndef __kernel_suseconds_t
typedef __kernel_long_t		__kernel_suseconds_t;
#endif

#ifndef __kernel_uid32_t
typedef unsigned int	__kernel_uid32_t;
typedef unsigned int	__kernel_gid32_t;
#endif

#ifndef __kernel_old_dev_t
typedef unsigned int	__kernel_old_dev_t;
#endif

/* 32-bit only kernel */
#ifndef __kernel_size_t
typedef unsigned int	__kernel_size_t;
typedef int		__kernel_ssize_t;
#endif

#ifndef __kernel_fsid_t
typedef struct {
	int	val[2];
} __kernel_fsid_t;
#endif

typedef long long	__kernel_loff_t;

#endif
