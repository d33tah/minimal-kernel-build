/* Stub d_path - path to string conversion */
#include <linux/fs.h>
#include <linux/syscalls.h>

char *simple_dname(struct dentry *dentry, char *buffer, int buflen) { return buffer; }

SYSCALL_DEFINE2(getcwd, char __user *, buf, unsigned long, size) { return -ENOSYS; }
