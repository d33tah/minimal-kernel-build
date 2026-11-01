#include <linux/fs.h>
#include <linux/file.h>
#include "internal.h"

static struct kmem_cache *filp_cachep __read_mostly;

static void file_free_rcu(struct rcu_head *head)
{
	struct file *f = container_of(head, struct file, f_u.fu_rcuhead);

	kfree(f);
}

static inline void file_free(struct file *f)
{
	/* One CPU, no preemption, no lockless fd lookup: free at once. */
	file_free_rcu(&f->f_u.fu_rcuhead);
}

static struct file *__alloc_file(int flags, const struct cred *cred)
{
	struct file *f;

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (unlikely(!f))
		return ERR_PTR(-ENOMEM);

	atomic_long_set(&f->f_count, 1);
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

	if (mode & FMODE_WRITER)
		put_write_access(inode);
	dput(dentry);
out:
	file_free(file);
}

void fput(struct file *file)
{
	if (atomic_long_dec_and_test(&file->f_count))
		__fput(file);
}

void __init files_init(void)
{
	filp_cachep = kmem_cache_create(
		"filp", sizeof(struct file), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);
}
