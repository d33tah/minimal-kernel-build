// SPDX-License-Identifier: GPL-2.0
/* Stubbed fcntl.c */
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/export.h>
#include <linux/syscalls.h>

void __f_setown(struct file *filp, struct pid *pid, enum pid_type type, int force) { }

int f_setown(struct file *filp, unsigned long arg, int force) { return 0; }

int fasync_helper(int fd, struct file * filp, int on, struct fasync_struct **fapp) { return 0; }

void kill_fasync(struct fasync_struct **fp, int sig, int band) { }

SYSCALL_DEFINE3(fcntl, unsigned int, fd, unsigned int, cmd, unsigned long, arg) { return -EINVAL; }
SYSCALL_DEFINE3(fcntl64, unsigned int, fd, unsigned int, cmd, unsigned long, arg) { return -EINVAL; }
