// SPDX-License-Identifier: GPL-2.0
/* Stubbed utimes.c */
#include <linux/syscalls.h>
#include <linux/utime.h>
#include <linux/fs.h>

int vfs_utimes(const struct path *path, struct timespec64 *times) { return 0; }

SYSCALL_DEFINE4(utimensat, int, dfd, const char __user *, filename,
		struct __kernel_timespec __user *, utimes, int, flags) { return 0; }
SYSCALL_DEFINE3(futimesat, int, dfd, const char __user *, filename,
		struct __kernel_old_timeval __user *, utimes) { return 0; }
SYSCALL_DEFINE2(utimes, char __user *, filename,
		struct __kernel_old_timeval __user *, utimes) { return 0; }
SYSCALL_DEFINE2(utime, char __user *, filename, struct utimbuf __user *, times) { return 0; }
