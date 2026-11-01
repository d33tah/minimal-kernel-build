#ifndef _ASM_X86_POSIX_TYPES_32_H
#define _ASM_X86_POSIX_TYPES_32_H

#include <asm/bitsperlong.h>

typedef unsigned long __kernel_ulong_t;

typedef int __kernel_pid_t;

typedef unsigned int __kernel_uid32_t;
typedef unsigned int __kernel_gid32_t;

/* 32-bit only kernel */
typedef unsigned int __kernel_size_t;
typedef int __kernel_ssize_t;

typedef long long __kernel_loff_t;

#endif
