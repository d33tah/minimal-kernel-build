
#ifndef _ASM_X86_POSIX_TYPES_32_H
#define _ASM_X86_POSIX_TYPES_32_H

typedef unsigned short __kernel_mode_t;

typedef unsigned short __kernel_ipc_pid_t;

typedef unsigned short __kernel_uid_t;

typedef unsigned short __kernel_old_dev_t;

/* Inlined from asm-generic/posix_types.h */
#include <asm/bitsperlong.h>

typedef long __kernel_long_t;
typedef unsigned long __kernel_ulong_t;

typedef __kernel_ulong_t __kernel_ino_t;

typedef int __kernel_pid_t;

typedef unsigned int __kernel_uid32_t;
typedef unsigned int __kernel_gid32_t;

/* 32-bit only kernel */
typedef unsigned int __kernel_size_t;
typedef int __kernel_ssize_t;

typedef struct {
	int val[2];
} __kernel_fsid_t;

typedef long long __kernel_loff_t;

#endif
