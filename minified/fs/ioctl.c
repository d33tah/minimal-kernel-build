#include <linux/fs.h>
#include <linux/export.h>
#include <linux/syscalls.h>

/* All helper functions removed - only stub syscall remains */

long vfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) { return -ENOTTY; }

SYSCALL_DEFINE3(ioctl, unsigned int, fd, unsigned int, cmd, unsigned long, arg) { return -ENOTTY; }
