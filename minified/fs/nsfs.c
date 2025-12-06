/* Minimal includes for nsfs stubs */
#include <linux/fs.h>
#include <linux/proc_ns.h>

/* --- 2025-12-06 20:38 --- nsfs.h inlined (13 LOC) */
#define NSIO	0xb7
#define NS_GET_USERNS		_IO(NSIO, 0x1)
#define NS_GET_PARENT		_IO(NSIO, 0x2)
#define NS_GET_NSTYPE		_IO(NSIO, 0x3)
#define NS_GET_OWNER_UID	_IO(NSIO, 0x4)
/* --- end nsfs.h inlined --- */

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
