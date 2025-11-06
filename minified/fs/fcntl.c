// SPDX-License-Identifier: GPL-2.0
/* Stubbed fcntl.c */
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/export.h>
#include <linux/syscalls.h>

void __f_setown(struct file *filp, struct pid *pid, enum pid_type type, int force) { }
EXPORT_SYMBOL(__f_setown);

int f_setown(struct file *filp, unsigned long arg, int force) { return 0; }
EXPORT_SYMBOL(f_setown);

int fasync_helper(int fd, struct file * filp, int on, struct fasync_struct **fapp) { return 0; }
EXPORT_SYMBOL(fasync_helper);

void kill_fasync(struct fasync_struct **fp, int sig, int band) { }
EXPORT_SYMBOL(kill_fasync);

SYSCALL_DEFINE3(fcntl, unsigned int, fd, unsigned int, cmd, unsigned long, arg) { return -EINVAL; }
SYSCALL_DEFINE3(fcntl64, unsigned int, fd, unsigned int, cmd, unsigned long, arg) { return -EINVAL; }
