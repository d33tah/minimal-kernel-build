
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fs.h>

#include "internal.h"

static struct kmem_cache *filp_cachep __read_mostly;

static void file_free_rcu(struct rcu_head *head)
{
	struct file *f = container_of(head, struct file, f_u.fu_rcuhead);

	put_cred(f->f_cred);
	kmem_cache_free(filp_cachep, f);
}

static inline void file_free(struct file *f)
{
	call_rcu(&f->f_u.fu_rcuhead, file_free_rcu);
}

static struct file *__alloc_file(int flags, const struct cred *cred)
{
	struct file *f;

	f = kmem_cache_zalloc(filp_cachep, GFP_KERNEL);
	if (unlikely(!f))
		return ERR_PTR(-ENOMEM);

	f->f_cred = get_cred(cred);

	atomic_long_set(&f->f_count, 1);
	f->f_flags = flags;
	f->f_mode = OPEN_FMODE(flags);

	return f;
}

struct file *alloc_empty_file(int flags, const struct cred *cred)
{
	struct file *f;

	f = __alloc_file(flags, cred);

	return f;
}

static void __fput(struct file *file)
{
	struct dentry *dentry = file->f_path.dentry;
	struct inode *inode = file->f_inode;
	fmode_t mode = file->f_mode;

	if (unlikely(!(file->f_mode & FMODE_OPENED)))
		goto out;

	might_sleep();

	if (file->f_op->release)
		file->f_op->release(inode, file);
	fops_put(file->f_op);
	if ((mode & (FMODE_READ | FMODE_WRITE)) == FMODE_READ)
		i_readcount_dec(inode);
	if (mode & FMODE_WRITER)
		put_write_access(inode);
	dput(dentry);
out:
	file_free(file);
}

static LLIST_HEAD(delayed_fput_list);
static void delayed_fput(struct work_struct *unused)
{
	struct llist_node *node = llist_del_all(&delayed_fput_list);
	struct file *f, *t;

	llist_for_each_entry_safe(f, t, node, f_u.fu_llist)
		__fput(f);
}

void flush_delayed_fput(void)
{
	delayed_fput(NULL);
}

static DECLARE_DELAYED_WORK(delayed_fput_work, delayed_fput);

void fput(struct file *file)
{
	if (atomic_long_dec_and_test(&file->f_count)) {
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
