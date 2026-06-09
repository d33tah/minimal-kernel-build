#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/syscalls.h>
#include <linux/export.h>

void generic_fillattr(struct user_namespace *mnt_userns, struct inode *inode,
		      struct kstat *stat) { }

int vfs_getattr(const struct path *path, struct kstat *stat,
		u32 request_mask, unsigned int query_flags) { return 0; }
