
#include <linux/namei.h>
#include <linux/file.h>
#include <linux/fs_struct.h>

#include "internal.h"
#include "mount.h"

#define EMBEDDED_NAME_MAX (PATH_MAX - offsetof(struct filename, iname))

struct filename *getname_kernel(const char *filename)
{
	struct filename *result;
	int len = strlen(filename) + 1;

	result = __getname();
	if (unlikely(!result))
		return ERR_PTR(-ENOMEM);

	if (len <= EMBEDDED_NAME_MAX) {
		result->name = (char *)result->iname;
	} else if (len <= PATH_MAX) {
		const size_t size = offsetof(struct filename, iname[1]);
		struct filename *tmp;

		tmp = kmalloc(size, GFP_KERNEL);
		if (unlikely(!tmp)) {
			__putname(result);
			return ERR_PTR(-ENOMEM);
		}
		tmp->name = (char *)result;
		result = tmp;
	} else {
		__putname(result);
		return ERR_PTR(-ENAMETOOLONG);
	}
	memcpy((char *)result->name, filename, len);

	result->refcnt = 1;
	return result;
}

void putname(struct filename *name)
{
	if (IS_ERR(name))
		return;

	BUG_ON(name->refcnt <= 0);

	if (--name->refcnt > 0)
		return;

	if (name->name != name->iname) {
		__putname(name->name);
		kfree(name);
	} else
		__putname(name);
}

int inode_permission(struct user_namespace *mnt_userns, struct inode *inode,
		     int mask)
{
	return 0;
}

void path_get(const struct path *path)
{
	mntget(path->mnt);
	dget(path->dentry);
}

void path_put(const struct path *path)
{
	dput(path->dentry);
	mntput(path->mnt);
}

#define EMBEDDED_LEVELS 2
struct nameidata {
	struct path path;
	struct qstr last;
	struct path root;
	struct inode *inode;
	unsigned int flags, state;
	unsigned seq, m_seq;
	int last_type;
	unsigned depth;
	struct saved {
		struct path link;
		struct delayed_call done;
		const char *name;
		unsigned seq;
	} *stack, internal[EMBEDDED_LEVELS];
	struct filename *name;
	struct nameidata *saved;
	unsigned root_seq;
	int dfd;
} __randomize_layout;

#define ND_ROOT_PRESET 1
#define ND_ROOT_GRABBED 2

static inline void set_nameidata(struct nameidata *p, int dfd,
				 struct filename *name, const struct path *root)
{
	struct nameidata *old = current->nameidata;
	p->stack = p->internal;
	p->depth = 0;
	p->dfd = dfd;
	p->name = name;
	p->path.mnt = NULL;
	p->path.dentry = NULL;
	p->saved = old;
	current->nameidata = p;
	p->state = 0;
}

static void restore_nameidata(void)
{
	struct nameidata *now = current->nameidata, *old = now->saved;

	current->nameidata = old;
	if (now->stack != now->internal)
		kfree(now->stack);
}

static void drop_links(struct nameidata *nd)
{
	int i = nd->depth;
	while (i--) {
		struct saved *last = nd->stack + i;
		do_delayed_call(&last->done);
		last->done.fn = NULL; /* inlined clear_delayed_call */
	}
}

static void terminate_walk(struct nameidata *nd)
{
	drop_links(nd);
	if (!(nd->flags & LOOKUP_RCU)) {
		int i;
		path_put(&nd->path);
		for (i = 0; i < nd->depth; i++)
			path_put(&nd->stack[i].link);
		if (nd->state & ND_ROOT_GRABBED) {
			path_put(&nd->root);
			nd->state &= ~ND_ROOT_GRABBED;
		}
	} else {
		nd->flags &= ~LOOKUP_RCU;
		rcu_read_unlock();
	}
	nd->depth = 0;
	nd->path.mnt = NULL;
	nd->path.dentry = NULL;
}

static inline bool legitimize_path(struct nameidata *nd, struct path *path,
				   unsigned seq)
{
	int res = __legitimize_mnt(path->mnt, nd->m_seq);
	if (unlikely(res)) {
		if (res > 0)
			path->mnt = NULL;
		path->dentry = NULL;
		return false;
	}
	if (unlikely(!lockref_get_not_dead(&path->dentry->d_lockref))) {
		path->dentry = NULL;
		return false;
	}
	return !read_seqcount_retry(&path->dentry->d_seq, seq);
}

static bool legitimize_links(struct nameidata *nd)
{
	int i;
	for (i = 0; i < nd->depth; i++) {
		struct saved *last = nd->stack + i;
		if (unlikely(!legitimize_path(nd, &last->link, last->seq))) {
			drop_links(nd);
			nd->depth = i + 1;
			return false;
		}
	}
	return true;
}

static bool legitimize_root(struct nameidata *nd)
{
	if (!nd->root.mnt)
		return true;
	nd->state |= ND_ROOT_GRABBED;
	return legitimize_path(nd, &nd->root, nd->root_seq);
}

static bool try_to_unlazy(struct nameidata *nd)
{
	struct dentry *parent = nd->path.dentry;

	BUG_ON(!(nd->flags & LOOKUP_RCU));

	nd->flags &= ~LOOKUP_RCU;
	if (unlikely(!legitimize_links(nd)))
		goto out1;
	if (unlikely(!legitimize_path(nd, &nd->path, nd->seq)))
		goto out;
	if (unlikely(!legitimize_root(nd)))
		goto out;
	rcu_read_unlock();
	BUG_ON(nd->inode != parent->d_inode);
	return true;

out1:
	nd->path.mnt = NULL;
	nd->path.dentry = NULL;
out:
	rcu_read_unlock();
	return false;
}

static bool try_to_unlazy_next(struct nameidata *nd, struct dentry *dentry,
			       unsigned seq)
{
	BUG_ON(!(nd->flags & LOOKUP_RCU));

	nd->flags &= ~LOOKUP_RCU;
	if (unlikely(!legitimize_links(nd)))
		goto out2;
	if (unlikely(!legitimize_mnt(nd->path.mnt, nd->m_seq)))
		goto out2;
	if (unlikely(!lockref_get_not_dead(&nd->path.dentry->d_lockref)))
		goto out1;

	if (unlikely(!lockref_get_not_dead(&dentry->d_lockref)))
		goto out;
	if (unlikely(read_seqcount_retry(&dentry->d_seq, seq)))
		goto out_dput;

	if (unlikely(!legitimize_root(nd)))
		goto out_dput;
	rcu_read_unlock();
	return true;

out2:
	nd->path.mnt = NULL;
out1:
	nd->path.dentry = NULL;
out:
	rcu_read_unlock();
	return false;
out_dput:
	rcu_read_unlock();
	dput(dentry);
	return false;
}

static int complete_walk(struct nameidata *nd)
{
	if (nd->flags & LOOKUP_RCU) {
		nd->root.mnt = NULL;
		if (!try_to_unlazy(nd))
			return -ECHILD;
	}
	return 0;
}

static int set_root(struct nameidata *nd)
{
	struct fs_struct *fs = current->fs;

	if (nd->flags & LOOKUP_RCU) {
		unsigned seq;

		do {
			seq = raw_read_seqcount_begin(&fs->seq);
			nd->root = fs->root;
			nd->root_seq =
				__read_seqcount_begin(&nd->root.dentry->d_seq);
		} while (read_seqcount_retry(&fs->seq, seq));
	} else {
		spin_lock(&fs->lock);
		nd->root = fs->root;
		path_get(&nd->root);
		spin_unlock(&fs->lock);
		nd->state |= ND_ROOT_GRABBED;
	}
	return 0;
}

static inline void put_link(struct nameidata *nd)
{
	struct saved *last = nd->stack + --nd->depth;
	do_delayed_call(&last->done);
	if (!(nd->flags & LOOKUP_RCU))
		path_put(&last->link);
}

static inline int handle_mounts(struct nameidata *nd, struct dentry *dentry,
				struct path *path, struct inode **inode,
				unsigned int *seqp)
{
	path->mnt = nd->path.mnt;
	path->dentry = dentry;
	if (nd->flags & LOOKUP_RCU) {
		unsigned int seq = *seqp;
		if (unlikely(!*inode))
			return -ENOENT;
		{
			struct dentry *rcu_dentry = path->dentry;
			unsigned int flags = rcu_dentry->d_flags;
			bool rcu_ok = true;

			if (likely(!(flags & DCACHE_MANAGED_DENTRY)))
				goto rcu_done;
			rcu_ok = false;
			for (;;) {
				if (flags & DCACHE_MOUNTED) {
					struct mount *mounted = __lookup_mnt(
						path->mnt, rcu_dentry);
					if (mounted) {
						path->mnt = &mounted->mnt;
						rcu_dentry = path->dentry =
							mounted->mnt.mnt_root;

						*seqp = raw_read_seqcount_begin(
							&rcu_dentry->d_seq);
						*inode = rcu_dentry->d_inode;
						flags = rcu_dentry->d_flags;
						continue;
					}
					if (read_seqretry(&mount_lock,
							  nd->m_seq))
						goto rcu_fail;
				}
				rcu_ok = !(flags & DCACHE_NEED_AUTOMOUNT);
				break;
			}
rcu_done:
			if (likely(rcu_ok))
				return 0;
rcu_fail:;
		}
		if (!try_to_unlazy_next(nd, dentry, seq))
			return -ECHILD;

		path->mnt = nd->path.mnt;
		path->dentry = dentry;
	}
	{
		unsigned flags = smp_load_acquire(&path->dentry->d_flags);
		if (unlikely((flags & DCACHE_ENTRY_TYPE) ==
			     DCACHE_MISS_TYPE)) { /* d_flags_negative inlined */
			dput(path->dentry);
			if (path->mnt != nd->path.mnt)
				mntput(path->mnt);
			return -ENOENT;
		}
	}
	*inode = d_inode(path->dentry);
	*seqp = 0;
	return 0;
}

static struct dentry *lookup_fast(struct nameidata *nd, struct inode **inode,
				  unsigned *seqp)
{
	struct dentry *dentry, *parent = nd->path.dentry;

	if (nd->flags & LOOKUP_RCU) {
		unsigned seq;
		dentry = __d_lookup_rcu(parent, &nd->last, &seq);
		if (unlikely(!dentry)) {
			if (!try_to_unlazy(nd))
				return ERR_PTR(-ECHILD);
			return NULL;
		}

		*inode = d_inode(dentry);
		if (unlikely(read_seqcount_retry(&dentry->d_seq, seq)))
			return ERR_PTR(-ECHILD);

		if (unlikely(__read_seqcount_retry(&parent->d_seq, nd->seq)))
			return ERR_PTR(-ECHILD);

		*seqp = seq;
	} else {
		dentry = __d_lookup(parent, &nd->last);
		if (unlikely(!dentry))
			return NULL;
	}
	return dentry;
}

enum { WALK_TRAILING = 1, WALK_MORE = 2, WALK_NOFOLLOW = 4 };

static const char *step_into(struct nameidata *nd, int flags,
			     struct dentry *dentry, struct inode *inode,
			     unsigned seq)
{
	struct path path;
	int err = handle_mounts(nd, dentry, &path, &inode, &seq);

	if (err < 0)
		return ERR_PTR(err);
	if (!(nd->flags & LOOKUP_RCU)) {
		dput(nd->path.dentry);
		if (nd->path.mnt != path.mnt)
			mntput(nd->path.mnt);
	}
	nd->path = path;
	nd->inode = inode;
	nd->seq = seq;
	return NULL;
}

static const char *handle_dots(struct nameidata *nd, int type)
{
	return NULL;
}

static const char *walk_component(struct nameidata *nd, int flags)
{
	struct dentry *dentry;
	struct inode *inode;
	unsigned seq;

	if (unlikely(nd->last_type != LAST_NORM)) {
		if (!(flags & WALK_MORE) && nd->depth)
			put_link(nd);
		return handle_dots(nd, nd->last_type);
	}
	dentry = lookup_fast(nd, &inode, &seq);
	if (IS_ERR(dentry))
		return ERR_CAST(dentry);
	if (unlikely(!dentry)) {
		struct inode *dir_inode = nd->path.dentry->d_inode;
		struct dentry *old;
		DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);

		inode_lock_shared(dir_inode);
		if (unlikely(IS_DEADDIR(dir_inode))) {
			inode_unlock_shared(dir_inode);
			return ERR_PTR(-ENOENT);
		}
		dentry = d_alloc_parallel(nd->path.dentry, &nd->last, &wq);
		if (IS_ERR(dentry)) {
			inode_unlock_shared(dir_inode);
			return ERR_CAST(dentry);
		}
		if (likely(d_in_lookup(dentry))) {
			old = dir_inode->i_op->lookup(dir_inode, dentry,
						      nd->flags);
			d_lookup_done(dentry);
			if (unlikely(old)) {
				dput(dentry);
				dentry = old;
			}
		}
		inode_unlock_shared(dir_inode);
		if (IS_ERR(dentry))
			return ERR_CAST(dentry);
	}
	if (!(flags & WALK_MORE) && nd->depth)
		put_link(nd);
	return step_into(nd, flags, dentry, inode, seq);
}

#include <asm/word-at-a-time.h>

#define HASH_MIX(x, y, a) \
	(x ^= (a), y ^= x, x = rol32(x, 7), x += y, y = rol32(y, 20), y *= 9)

static inline u64 hash_name(const void *salt, const char *name)
{
	unsigned long a = 0, b, x = 0, y = (unsigned long)salt;
	unsigned long adata, bdata, mask, len;
	const struct word_at_a_time constants = WORD_AT_A_TIME_CONSTANTS;

	len = 0;
	goto inside;

	do {
		HASH_MIX(x, y, a);
		len += sizeof(unsigned long);
inside:
		a = load_unaligned_zeropad(name + len);
		b = a ^ REPEAT_BYTE('/');
	} while (!(has_zero(a, &adata, &constants) |
		   has_zero(b, &bdata, &constants)));

	adata = prep_zero_mask(a, adata, &constants);
	bdata = prep_zero_mask(b, bdata, &constants);
	mask = create_zero_mask(adata | bdata);
	x ^= a & zero_bytemask(mask);

	return hashlen_create(__hash_32(y ^ __hash_32(x)),
			      len + find_zero(mask));
}

static int link_path_walk(const char *name, struct nameidata *nd)
{
	int depth = 0;

	nd->last_type = LAST_ROOT;
	nd->flags |= LOOKUP_PARENT;
	if (IS_ERR(name))
		return PTR_ERR(name);
	while (*name == '/')
		name++;
	if (!*name)
		return 0;

	for (;;) {
		const char *link;
		u64 hash_len;
		int type;

		hash_len = hash_name(nd->path.dentry, name);

		type = LAST_NORM;
		if (name[0] == '.')
			switch (hashlen_len(hash_len)) {
			case 2:
				if (name[1] == '.') {
					type = LAST_DOTDOT;
				}
				break;
			case 1:
				type = LAST_DOT;
			}

		nd->last.hash_len = hash_len;
		nd->last.name = name;
		nd->last_type = type;

		name += hashlen_len(hash_len);
		if (!*name)
			goto OK;

		do {
			name++;
		} while (unlikely(*name == '/'));
		if (unlikely(!*name)) {
OK:

			if (!depth) {
				nd->flags &= ~LOOKUP_PARENT;
				return 0;
			}

			name = nd->stack[--depth].name;
			link = walk_component(nd, 0);
		} else {
			link = walk_component(nd, WALK_MORE);
		}
		if (unlikely(link)) {
			if (IS_ERR(link))
				return PTR_ERR(link);

			nd->stack[depth++].name = name;
			name = link;
			continue;
		}
		if (unlikely(!d_can_lookup(nd->path.dentry))) {
			if (nd->flags & LOOKUP_RCU) {
				if (!try_to_unlazy(nd))
					return -ECHILD;
			}
			return -ENOTDIR;
		}
	}
}

static const char *path_init(struct nameidata *nd, unsigned flags)
{
	int error;
	const char *s = nd->name->name;

	if (!*s)
		flags &= ~LOOKUP_RCU;
	if (flags & LOOKUP_RCU)
		rcu_read_lock();

	nd->flags = flags;

	nd->m_seq = __read_seqcount_begin(&mount_lock.seqcount);
	smp_rmb();

	nd->root.mnt = NULL;

	if (*s == '/') {
		error = set_root(nd);
		if (error)
			return ERR_PTR(error);
		if (nd->flags & LOOKUP_RCU) {
			struct dentry *d;
			nd->path = nd->root;
			d = nd->path.dentry;
			nd->inode = d->d_inode;
			nd->seq = nd->root_seq;
			if (unlikely(read_seqcount_retry(&d->d_seq, nd->seq)))
				return ERR_PTR(-ECHILD);
		} else {
			path_put(&nd->path);
			nd->path = nd->root;
			path_get(&nd->path);
			nd->inode = nd->path.dentry->d_inode;
		}
		return s;
	}

	spin_lock(&current->fs->lock);
	nd->path = current->fs->pwd;
	path_get(&nd->path);
	spin_unlock(&current->fs->lock);
	nd->inode = nd->path.dentry->d_inode;

	return s;
}

static int path_lookupat(struct nameidata *nd, unsigned flags,
			 struct path *path)
{
	const char *s = path_init(nd, flags);
	int err;

	while (!(err = link_path_walk(s, nd))) {
		if (nd->last_type == LAST_NORM && nd->last.name[nd->last.len])
			nd->flags |= LOOKUP_FOLLOW | LOOKUP_DIRECTORY;
		s = walk_component(nd, WALK_TRAILING);
		if (s == NULL)
			break;
	}
	if (!err)
		err = complete_walk(nd);

	if (!err && nd->flags & LOOKUP_DIRECTORY)
		if (!d_can_lookup(nd->path.dentry))
			err = -ENOTDIR;
	if (!err) {
		*path = nd->path;
		nd->path.mnt = NULL;
		nd->path.dentry = NULL;
	}
	terminate_walk(nd);
	return err;
}

int kern_path(const char *name, unsigned int flags, struct path *path)
{
	struct filename *filename = getname_kernel(name);
	int ret;
	struct nameidata nd;

	if (IS_ERR(filename))
		return PTR_ERR(filename);
	set_nameidata(&nd, AT_FDCWD, filename, NULL);
	ret = path_lookupat(&nd, flags | LOOKUP_RCU, path);
	if (unlikely(ret == -ECHILD))
		ret = path_lookupat(&nd, flags, path);
	restore_nameidata();
	putname(filename);
	return ret;
}

static struct dentry *lookup_open(struct nameidata *nd, struct file *file,
				  const struct open_flags *op)
{
	struct dentry *dir = nd->path.dentry;
	struct inode *dir_inode = dir->d_inode;
	struct dentry *dentry;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);

	if (unlikely(IS_DEADDIR(dir_inode)))
		return ERR_PTR(-ENOENT);

	file->f_mode &= ~FMODE_CREATED;
	dentry = d_lookup(dir, &nd->last);
	if (!dentry) {
		dentry = d_alloc_parallel(dir, &nd->last, &wq);
		if (IS_ERR(dentry))
			return dentry;
	}
	if (d_in_lookup(dentry)) {
		struct dentry *res =
			dir_inode->i_op->lookup(dir_inode, dentry, nd->flags);
		d_lookup_done(dentry);
		if (IS_ERR(res)) {
			dput(dentry);
			return res;
		}
		if (res) {
			dput(dentry);
			dentry = res;
		}
	}
	return dentry;
}

static const char *open_last_lookups(struct nameidata *nd, struct file *file,
				     const struct open_flags *op)
{
	unsigned seq;
	struct inode *inode;
	struct dentry *dentry;
	const char *res;

	nd->flags |= op->intent;

	if (nd->last_type != LAST_NORM) {
		if (nd->depth)
			put_link(nd);
		return handle_dots(nd, nd->last_type);
	}

	if (nd->last.name[nd->last.len])
		nd->flags |= LOOKUP_FOLLOW | LOOKUP_DIRECTORY;

	dentry = lookup_fast(nd, &inode, &seq);
	if (IS_ERR(dentry))
		return ERR_CAST(dentry);
	if (likely(dentry))
		goto finish_lookup;

	BUG_ON(nd->flags & LOOKUP_RCU);

	/* Slow path lookup - no O_CREAT needed */
	{
		struct dentry *dir = nd->path.dentry;
		inode_lock_shared(dir->d_inode);
		dentry = lookup_open(nd, file, op);
		inode_unlock_shared(dir->d_inode);
	}

	if (IS_ERR(dentry))
		return ERR_CAST(dentry);

	if (file->f_mode & (FMODE_OPENED | FMODE_CREATED)) {
		dput(nd->path.dentry);
		nd->path.dentry = dentry;
		return NULL;
	}

finish_lookup:
	if (nd->depth)
		put_link(nd);
	res = step_into(nd, WALK_TRAILING, dentry, inode, seq);
	if (unlikely(res))
		nd->flags &= ~(LOOKUP_OPEN | LOOKUP_CREATE | LOOKUP_EXCL);
	return res;
}

static int do_open(struct nameidata *nd, struct file *file,
		   const struct open_flags *op)
{
	int error;

	if (!(file->f_mode & (FMODE_OPENED | FMODE_CREATED))) {
		error = complete_walk(nd);
		if (error)
			return error;
	}
	if ((nd->flags & LOOKUP_DIRECTORY) && !d_can_lookup(nd->path.dentry))
		return -ENOTDIR;

	if (!nd->path.dentry->d_inode)
		return -ENOENT;

	if (!(file->f_mode & FMODE_OPENED))
		error = vfs_open(&nd->path, file);
	else
		error = 0;
	return error;
}

static struct file *path_openat(struct nameidata *nd,
				const struct open_flags *op, unsigned flags)
{
	struct file *file;
	int error;

	file = alloc_empty_file(op->open_flag, current_cred());
	if (IS_ERR(file))
		return file;

	{
		const char *s = path_init(nd, flags);
		while (!(error = link_path_walk(s, nd)) &&
		       (s = open_last_lookups(nd, file, op)) != NULL)
			;
		if (!error)
			error = do_open(nd, file, op);
		terminate_walk(nd);
	}
	if (likely(!error)) {
		if (likely(file->f_mode & FMODE_OPENED))
			return file;
		WARN_ON(1);
		error = -EINVAL;
	}
	fput(file);
	return ERR_PTR(error);
}

struct file *do_filp_open(int dfd, struct filename *pathname,
			  const struct open_flags *op)
{
	struct nameidata nd;
	int flags = op->lookup_flags;
	struct file *filp;

	set_nameidata(&nd, dfd, pathname, NULL);
	filp = path_openat(&nd, op, flags | LOOKUP_RCU);
	if (unlikely(filp == ERR_PTR(-ECHILD)))
		filp = path_openat(&nd, op, flags);
	restore_nameidata();
	return filp;
}
