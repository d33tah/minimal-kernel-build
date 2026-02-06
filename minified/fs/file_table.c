
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/security.h>
#include <linux/cred.h>
#include <linux/rcupdate.h>
#include <linux/mount.h>
#include <linux/cdev.h>
#include <linux/percpu_counter.h>
#include <linux/percpu.h>
#include <linux/task_work.h>

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
	/* f_lock initialization removed - f_lock removed from struct file */
	mutex_init(&f->f_pos_lock);
	f->f_flags = flags;
	f->f_mode = OPEN_FMODE(flags);
	return f;
}

/* alloc_file_pseudo removed - declared/defined but never called (~35 LOC) */


static void __fput(struct file *file)
{
	struct dentry *dentry = file->f_path.dentry;
	struct vfsmount *mnt = file->f_path.mnt;
	struct inode *inode = file->f_inode;
	fmode_t mode = file->f_mode;

	if (unlikely(!(file->f_mode & FMODE_OPENED)))
		goto out;

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

/* Merged from lib/llist.c */
bool llist_add_batch(struct llist_node *new_first, struct llist_node *new_last,
		     struct llist_head *head)
{
	struct llist_node *first;
	do {
		new_last->next = first = READ_ONCE(head->first);
	} while (cmpxchg(&head->first, first, new_first) != first);
	return !first;
}
