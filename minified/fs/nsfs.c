#include <linux/mount.h>
#include <linux/pseudo_fs.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/proc_ns.h>
#include <linux/magic.h>
#include <linux/ktime.h>
#include <linux/seq_file.h>
#include <linux/user_namespace.h>
#include <linux/nsfs.h>
#include <linux/uaccess.h>

#include "internal.h"


const struct dentry_operations ns_dentry_operations = {
	.d_delete	= always_delete_dentry,
};

int ns_get_path_cb(struct path *path, ns_get_path_helper_t *ns_get_cb,
		     void *private_data)
{
	return -ENOSYS;
}

int ns_get_path(struct path *path, struct task_struct *task,
		  const struct proc_ns_operations *ns_ops)
{
	return -ENOSYS;
}

int open_related_ns(struct ns_common *ns,
		   struct ns_common *(*get_ns)(struct ns_common *ns))
{
	return -ENOSYS;
}

int ns_get_name(char *buf, size_t size, struct task_struct *task,
			const struct proc_ns_operations *ns_ops)
{
	return -ENOENT;
}

bool proc_ns_file(const struct file *file)
{
	return false;
}

struct file *proc_ns_fget(int fd)
{
	return ERR_PTR(-EINVAL);
}

bool ns_match(const struct ns_common *ns, dev_t dev, ino_t ino)
{
	return false;
}

void __init nsfs_init(void)
{
	 
}
