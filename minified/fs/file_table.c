#include <linux/security.h>
/* cdev.h inlined */
#include <linux/device.h>
struct file_operations;
struct inode;
struct module;
struct cdev {
	struct kobject kobj;
	struct module *owner;
	const struct file_operations *ops;
	struct list_head list;
	dev_t dev;
	unsigned int count;
} __randomize_layout;
void cdev_put(struct cdev *p);
void cd_forget(struct inode *);
#include <linux/task_work.h>

static struct kmem_cache *filp_cachep __read_mostly;

static void file_free_rcu(struct rcu_head *head)
{
	struct file *f = container_of(head, struct file, f_u.fu_rcuhead);

	kmem_cache_free(filp_cachep, f);
}

struct file *alloc_empty_file(int flags, const struct cred *cred)
{
	struct file *f;

	f = kmem_cache_zalloc(filp_cachep, GFP_KERNEL);
	if (unlikely(!f))
		return ERR_PTR(-ENOMEM);

	atomic_long_set(&f->f_count, 1);

	f->f_flags = flags;
	f->f_mode = OPEN_FMODE(flags);
	return f;
}

static void __fput(struct file *file)
{
	struct dentry *dentry = file->f_path.dentry;
	struct vfsmount *mnt = file->f_path.mnt;
	struct inode *inode = file->f_inode;
	fmode_t mode = file->f_mode;

	if (unlikely(!(file->f_mode & FMODE_OPENED)))
		goto out;

	if (file->f_op->release)
		file->f_op->release(inode, file);
	if (unlikely(S_ISCHR(inode->i_mode) && inode->i_cdev != NULL &&
		     !(mode & FMODE_PATH))) {
		cdev_put(inode->i_cdev);
	}
	fops_put(file->f_op);
	if (mode & FMODE_WRITER) {
		put_write_access(inode);
		__mnt_drop_write(mnt);
	}
	dput(dentry);
	mntput(mnt);
out:
	call_rcu(&file->f_u.fu_rcuhead, file_free_rcu);
}

static void ____fput(struct callback_head *work)
{
	__fput(container_of(work, struct file, f_u.fu_rcuhead));
}

void fput(struct file *file)
{
	if (atomic_long_dec_and_test(&file->f_count)) {
		struct task_struct *task = current;

		init_task_work(&file->f_u.fu_rcuhead, ____fput);
		if (!task_work_add(task, &file->f_u.fu_rcuhead, TWA_RESUME))
			return;

		/* Fallback: shouldn't happen in this minimal kernel */
		__fput(file);
	}
}

void __init files_init(void)
{
	filp_cachep = kmem_cache_create(
		"filp", sizeof(struct file), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);
}
