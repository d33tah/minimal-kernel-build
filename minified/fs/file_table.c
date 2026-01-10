
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/security.h>
#include <linux/cred.h>
#include <linux/eventpoll.h>
#include <linux/rcupdate.h>
#include <linux/mount.h>
#include <linux/capability.h>
#include <linux/cdev.h>
#include <linux/sysctl.h>
#include <linux/percpu_counter.h>
#include <linux/percpu.h>
#include <linux/task_work.h>
#include <linux/swap.h>

#include <linux/atomic.h>

#include "internal.h"

/* files_stat removed - only written, never read */

static struct kmem_cache *filp_cachep __read_mostly;

/* nr_files counter removed - only written, never read */

static void file_free_rcu(struct rcu_head *head)
{
	struct file *f = container_of(head, struct file, f_u.fu_rcuhead);

	put_cred(f->f_cred);
	kmem_cache_free(filp_cachep, f);
}

/* get_nr_files removed - unused */

struct file *alloc_empty_file(int flags, const struct cred *cred)
{
	struct file *f;

	/* capable() always returns true - file limit check removed */
	f = kmem_cache_zalloc(filp_cachep, GFP_KERNEL);
	if (unlikely(!f))
		return ERR_PTR(-ENOMEM);

	f->f_cred = get_cred(cred);
	/* security_file_alloc always returns 0 - dead code removed */
	atomic_long_set(&f->f_count, 1);
	/* f_owner struct removed - never accessed */
	spin_lock_init(&f->f_lock);
	mutex_init(&f->f_pos_lock);
	f->f_flags = flags;
	f->f_mode = OPEN_FMODE(flags);
	return f;
}

static struct file *alloc_file(const struct path *path, int flags,
			       const struct file_operations *fop)
{
	struct file *file;

	file = alloc_empty_file(flags, current_cred());
	if (IS_ERR(file))
		return file;

	file->f_path = *path;
	file->f_inode = path->dentry->d_inode;
	file->f_mapping = path->dentry->d_inode->i_mapping;
	if ((file->f_mode & FMODE_READ) && likely(fop->read || fop->read_iter))
		file->f_mode |= FMODE_CAN_READ;
	if ((file->f_mode & FMODE_WRITE) &&
	    likely(fop->write || fop->write_iter))
		file->f_mode |= FMODE_CAN_WRITE;
	file->f_mode |= FMODE_OPENED;
	file->f_op = fop;
	return file;
}

struct file *alloc_file_pseudo(struct inode *inode, struct vfsmount *mnt,
			       const char *name, int flags,
			       const struct file_operations *fops)
{
	static const struct dentry_operations anon_ops = {
		/* d_dname assignment removed - callback never called */
	};
	struct qstr this = QSTR_INIT(name, strlen(name));
	struct path path;
	struct file *file;

	path.dentry = d_alloc_pseudo(mnt->mnt_sb, &this);
	if (!path.dentry)
		return ERR_PTR(-ENOMEM);
	/* s_d_op check removed - always NULL (dops never set) */
	d_set_d_op(path.dentry, &anon_ops);
	path.mnt = mntget(mnt);
	d_instantiate(path.dentry, inode);
	file = alloc_file(&path, flags, fops);
	if (IS_ERR(file)) {
		ihold(inode);
		path_put(&path);
	}
	return file;
}

static void __fput(struct file *file)
{
	struct dentry *dentry = file->f_path.dentry;
	struct vfsmount *mnt = file->f_path.mnt;
	struct inode *inode = file->f_inode;
	fmode_t mode = file->f_mode;

	if (unlikely(!(file->f_mode & FMODE_OPENED)))
		goto out;

	might_sleep();

	/* FASYNC handling removed - fcntl returns EINVAL, FASYNC never set */
	if (file->f_op->release)
		file->f_op->release(inode, file);
	if (unlikely(S_ISCHR(inode->i_mode) && inode->i_cdev != NULL &&
		     !(mode & FMODE_PATH))) {
		cdev_put(inode->i_cdev);
	}
	fops_put(file->f_op);
	/* f_owner struct removed entirely */
	if (mode & FMODE_WRITER) {
		put_write_access(inode);
		__mnt_drop_write(mnt);
	}
	dput(dentry);
	if (unlikely(mode & FMODE_NEED_UNMOUNT))
		dissolve_on_fput(mnt);
	mntput(mnt);
out:
	call_rcu(&file->f_u.fu_rcuhead, file_free_rcu);
}

static LLIST_HEAD(delayed_fput_list);
static void delayed_fput(struct work_struct *unused)
{
	struct llist_node *node = llist_del_all(&delayed_fput_list);
	struct file *f, *t;

	llist_for_each_entry_safe(f, t, node, f_u.fu_llist)
		__fput(f);
}

static void ____fput(struct callback_head *work)
{
	__fput(container_of(work, struct file, f_u.fu_rcuhead));
}

/* flush_delayed_fput removed - never called */

static DECLARE_DELAYED_WORK(delayed_fput_work, delayed_fput);

void fput(struct file *file)
{
	if (atomic_long_dec_and_test(&file->f_count)) {
		struct task_struct *task = current;

		if (likely(!in_interrupt() && !(task->flags & PF_KTHREAD))) {
			init_task_work(&file->f_u.fu_rcuhead, ____fput);
			if (!task_work_add(task, &file->f_u.fu_rcuhead,
					   TWA_RESUME))
				return;
		}

		if (llist_add(&file->f_u.fu_llist, &delayed_fput_list))
			schedule_delayed_work(&delayed_fput_work, 1);
	}
}

void __init files_init(void)
{
	filp_cachep = kmem_cache_create(
		"filp", sizeof(struct file), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);
}

/* files_maxfiles_init removed entirely - was empty stub */
