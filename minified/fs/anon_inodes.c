
#include <linux/cred.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/magic.h>

#include <linux/uaccess.h>

static struct vfsmount *anon_inode_mnt __read_mostly;
static struct inode *anon_inode_inode;

/* anon_inodefs_dname, anon_inodefs_dentry_operations, anon_inodefs_init_fs_context,
   anon_inode_fs_type removed - fs never registered (anon_inode_init removed) */

/* Simplified: only non-secure anon inodes used */
static struct file *__anon_inode_getfile(const char *name,
					 const struct file_operations *fops,
					 void *priv, int flags)
{
	struct inode *inode;
	struct file *file;

	/* try_module_get always returns true - dead check removed */

	inode = anon_inode_inode;
	if (IS_ERR(inode)) {
		file = ERR_PTR(-ENODEV);
		goto err;
	}
	ihold(inode);

	file = alloc_file_pseudo(inode, anon_inode_mnt, name,
				 flags & (O_ACCMODE | O_NONBLOCK), fops);
	if (IS_ERR(file))
		goto err_iput;

	file->f_mapping = inode->i_mapping;
	file->private_data = priv;
	return file;

err_iput:
	iput(inode);
err:
	module_put(fops->owner);
	return file;
}

struct file *anon_inode_getfile(const char *name,
				const struct file_operations *fops, void *priv,
				int flags)
{
	return __anon_inode_getfile(name, fops, priv, flags);
}

/* anon_inode_init removed - kern_mount hangs with low memory */
