/* Stub d_path - path to string conversion */
#include <linux/fs.h>
#include <linux/syscalls.h>
char *d_path(const struct path *path, char *buf, int buflen)
{
	return buf;
}
char *dynamic_dname(struct dentry *dentry, char *buffer, int buflen,
		    const char *fmt, ...)
{
	return buffer;
}
/* simple_dname removed - d_dname callback never called */
