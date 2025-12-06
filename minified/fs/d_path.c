/* Stub d_path - path to string conversion */
#include <linux/fs.h>
#include <linux/syscalls.h>

char *d_path(const struct path *path, char *buf, int buflen) { return buf; }

char *dentry_path_raw(const struct dentry *dentry, char *buf, int buflen) { return buf; }

char *dynamic_dname(struct dentry *dentry, char *buffer, int buflen,
		    const char *fmt, ...) { return buffer; }

char *simple_dname(struct dentry *dentry, char *buffer, int buflen) { return buffer; }

SYSCALL_DEFINE2(getcwd, char __user *, buf, unsigned long, size) { return -ENOSYS; }
