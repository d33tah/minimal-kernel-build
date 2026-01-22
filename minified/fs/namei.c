
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/sched/mm.h>
#include <linux/security.h>
/* Removed: ima_file_check, devcgroup_inode_permission, devcgroup_inode_mknod
 * - Always returned 0 (no-op stubs) */
#include <linux/syscalls.h>
#include <linux/mount.h>
#include <linux/capability.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/fs_struct.h>

#include <linux/hash.h>
#include <linux/uaccess.h>

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
	result->uptr = NULL;
	result->refcnt = 1;
	/* audit_getname - empty stub */
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

int generic_permission(struct user_namespace *mnt_userns, struct inode *inode,
		       int mask)
{
	int ret;
	unsigned int mode = inode->i_mode;
	kuid_t i_uid;

	i_uid = i_uid_into_mnt(mnt_userns, inode);
	if (likely(uid_eq(current_fsuid(), i_uid))) {
		mask &= 7;
		mode >>= 6;
		ret = (mask & ~mode) ? -EACCES : 0;
	} else {
		mask &= 7;

		if (mask & (mode ^ (mode >> 3))) {
			kgid_t kgid = i_gid_into_mnt(mnt_userns, inode);
			if (in_group_p(kgid))
				mode >>= 3;
		}

		ret = (mask & ~mode) ? -EACCES : 0;
	}
	if (ret != -EACCES)
		return ret;

	if (S_ISDIR(inode->i_mode)) {
		if (!(mask & MAY_WRITE))
			if (capable_wrt_inode_uidgid(mnt_userns, inode,
						     CAP_DAC_READ_SEARCH))
				return 0;
		if (capable_wrt_inode_uidgid(mnt_userns, inode,
					     CAP_DAC_OVERRIDE))
			return 0;
		return -EACCES;
	}

	mask &= MAY_READ | MAY_WRITE | MAY_EXEC;
	if (mask == MAY_READ)
		if (capable_wrt_inode_uidgid(mnt_userns, inode,
					     CAP_DAC_READ_SEARCH))
			return 0;

	if (!(mask & MAY_EXEC) || (inode->i_mode & S_IXUGO))
		if (capable_wrt_inode_uidgid(mnt_userns, inode,
					     CAP_DAC_OVERRIDE))
			return 0;

	return -EACCES;
}

/* do_inode_permission inlined into inode_permission */

int inode_permission(struct user_namespace *mnt_userns, struct inode *inode,
		     int mask)
{
	if (unlikely(mask & MAY_WRITE)) {
		umode_t mode = inode->i_mode;
		if (sb_rdonly(inode->i_sb) && (S_ISREG(mode) || S_ISDIR(mode)))
			return -EROFS;
		if (IS_IMMUTABLE(inode))
			return -EPERM;
		if (HAS_UNMAPPED_ID(mnt_userns, inode))
			return -EACCES;
	}

	/* do_inode_permission inlined */
	if (unlikely(!(inode->i_opflags & IOP_FASTPERM))) {
		if (likely(inode->i_op->permission))
			return inode->i_op->permission(mnt_userns, inode, mask);

		spin_lock(&inode->i_lock);
		inode->i_opflags |= IOP_FASTPERM;
		spin_unlock(&inode->i_lock);
	}
	return generic_permission(mnt_userns, inode, mask);
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
	/* r_seq removed - only written, never read */
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
#define ND_JUMPED 4

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
	if (unlikely(root)) {
		p->state = ND_ROOT_PRESET;
		p->root = *root;
	}
}

static void restore_nameidata(void)
{
	struct nameidata *now = current->nameidata, *old = now->saved;

	current->nameidata = old;
	if (now->stack != now->internal)
		kfree(now->stack);
}

/* nd_alloc_stack removed - only called from pick_link which was removed */

static bool path_connected(struct vfsmount *mnt, struct dentry *dentry)
{
	struct super_block *sb = mnt->mnt_sb;

	if (mnt->mnt_root == sb->s_root)
		return true;

	return is_subdir(dentry, mnt->mnt_root);
}

static void drop_links(struct nameidata *nd)
{
	int i = nd->depth;
	while (i--) {
		struct saved *last = nd->stack + i;
		do_delayed_call(&last->done);
		clear_delayed_call(&last->done);
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
	if (!nd->root.mnt || (nd->state & ND_ROOT_PRESET))
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

/* d_revalidate removed - always returned 1, callers simplified (~6 LOC) */

static int complete_walk(struct nameidata *nd)
{
	/* Stub: simplified walk completion for minimal kernel */
	if (nd->flags & LOOKUP_RCU) {
		if (!(nd->state & ND_ROOT_PRESET))
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
			seq = read_seqcount_begin(&fs->seq);
			nd->root = fs->root;
			nd->root_seq =
				__read_seqcount_begin(&nd->root.dentry->d_seq);
		} while (read_seqcount_retry(&fs->seq, seq));
	} else {
		get_fs_root(fs, &nd->root);
		nd->state |= ND_ROOT_GRABBED;
	}
	return 0;
}

static int nd_jump_root(struct nameidata *nd)
{
	if (!nd->root.mnt) {
		int error = set_root(nd);
		if (error)
			return error;
	}
	if (nd->flags & LOOKUP_RCU) {
		struct dentry *d;
		nd->path = nd->root;
		d = nd->path.dentry;
		nd->inode = d->d_inode;
		nd->seq = nd->root_seq;
		if (unlikely(read_seqcount_retry(&d->d_seq, nd->seq)))
			return -ECHILD;
	} else {
		path_put(&nd->path);
		nd->path = nd->root;
		path_get(&nd->path);
		nd->inode = nd->path.dentry->d_inode;
	}
	nd->state |= ND_JUMPED;
	return 0;
}

static inline void put_link(struct nameidata *nd)
{
	struct saved *last = nd->stack + --nd->depth;
	do_delayed_call(&last->done);
	if (!(nd->flags & LOOKUP_RCU))
		path_put(&last->link);
}

/* may_linkat removed - only caller was init_link which was removed */

/* Removed: __traverse_mounts - inlined into traverse_mounts */
/* Removed: traverse_mounts - inlined into handle_mounts (~9 LOC) */

/* __follow_mount_rcu inlined into handle_mounts */

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
		/* Inlined __follow_mount_rcu */
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
						nd->state |= ND_JUMPED;
						*seqp = read_seqcount_begin(
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
	/* Inlined traverse_mounts - jumped is always false in minimal config */
	{
		unsigned flags = smp_load_acquire(&path->dentry->d_flags);
		if (unlikely(d_flags_negative(flags))) {
			dput(path->dentry);
			if (path->mnt != nd->path.mnt)
				mntput(path->mnt);
			return -ENOENT;
		}
	}
	*inode = d_backing_inode(path->dentry);
	*seqp = 0;
	return 0;
}

/* __lookup_hash removed - only caller was filename_create */

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

		*inode = d_backing_inode(dentry);
		if (unlikely(read_seqcount_retry(&dentry->d_seq, seq)))
			return ERR_PTR(-ECHILD);

		if (unlikely(__read_seqcount_retry(&parent->d_seq, nd->seq)))
			return ERR_PTR(-ECHILD);

		*seqp = seq;
		/* d_revalidate always returned 1 - removed call */
	} else {
		dentry = __d_lookup(parent, &nd->last);
		if (unlikely(!dentry))
			return NULL;
		/* d_revalidate always returned 1 - removed call */
	}
	return dentry;
}

static struct dentry *__lookup_slow(const struct qstr *name, struct dentry *dir,
				    unsigned int flags)
{
	struct dentry *dentry, *old;
	struct inode *inode = dir->d_inode;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);

	if (unlikely(IS_DEADDIR(inode)))
		return ERR_PTR(-ENOENT);
	/* 'again' label removed - no goto references it */
	dentry = d_alloc_parallel(dir, name, &wq);
	if (IS_ERR(dentry))
		return dentry;
	/* d_revalidate always returned 1 (> 0), dead error handling removed */
	if (likely(d_in_lookup(dentry))) {
		old = inode->i_op->lookup(inode, dentry, flags);
		d_lookup_done(dentry);
		if (unlikely(old)) {
			dput(dentry);
			dentry = old;
		}
	}
	return dentry;
}

/* may_lookup inlined into link_path_walk */

/* pick_link function removed (~80 LOC) - never called since
 * d_is_symlink() is always false (no symlinks created) */

enum { WALK_TRAILING = 1, WALK_MORE = 2, WALK_NOFOLLOW = 4 };

static const char *step_into(struct nameidata *nd, int flags,
			     struct dentry *dentry, struct inode *inode,
			     unsigned seq)
{
	struct path path;
	int err = handle_mounts(nd, dentry, &path, &inode, &seq);

	if (err < 0)
		return ERR_PTR(err);
	/* Symlink handling removed: d_is_symlink() is always false since
	 * symlinks are never created (ramfs/shmem only use S_IFREG/S_IFDIR).
	 * Removed ~10 LOC including pick_link call. */
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

static struct dentry *follow_dotdot_rcu(struct nameidata *nd,
					struct inode **inodep, unsigned *seqp)
{
	struct dentry *parent, *old;

	if (path_equal(&nd->path, &nd->root))
		goto in_root;
	if (unlikely(nd->path.dentry == nd->path.mnt->mnt_root))
		goto in_root;
	old = nd->path.dentry;
	parent = old->d_parent;
	*inodep = parent->d_inode;
	*seqp = read_seqcount_begin(&parent->d_seq);
	if (unlikely(read_seqcount_retry(&old->d_seq, nd->seq)))
		return ERR_PTR(-ECHILD);
	if (unlikely(!path_connected(nd->path.mnt, parent)))
		return ERR_PTR(-ECHILD);
	return parent;
in_root:
	if (unlikely(read_seqretry(&mount_lock, nd->m_seq)))
		return ERR_PTR(-ECHILD);
	return NULL;
}

static struct dentry *follow_dotdot(struct nameidata *nd, struct inode **inodep,
				    unsigned *seqp)
{
	struct dentry *parent;

	if (path_equal(&nd->path, &nd->root))
		goto in_root;
	if (unlikely(nd->path.dentry == nd->path.mnt->mnt_root))
		goto in_root;

	parent = dget_parent(nd->path.dentry);
	if (unlikely(!path_connected(nd->path.mnt, parent))) {
		dput(parent);
		return ERR_PTR(-ENOENT);
	}
	*seqp = 0;
	*inodep = parent->d_inode;
	return parent;

in_root:
	dget(nd->path.dentry);
	return NULL;
}

static const char *handle_dots(struct nameidata *nd, int type)
{
	if (type == LAST_DOTDOT) {
		const char *error = NULL;
		struct dentry *parent;
		struct inode *inode;
		unsigned seq;

		if (!nd->root.mnt) {
			error = ERR_PTR(set_root(nd));
			if (error)
				return error;
		}
		if (nd->flags & LOOKUP_RCU)
			parent = follow_dotdot_rcu(nd, &inode, &seq);
		else
			parent = follow_dotdot(nd, &inode, &seq);
		if (IS_ERR(parent))
			return ERR_CAST(parent);
		if (unlikely(!parent))
			error = step_into(nd, WALK_NOFOLLOW, nd->path.dentry,
					  nd->inode, nd->seq);
		else
			error = step_into(nd, WALK_NOFOLLOW, parent, inode,
					  seq);
		if (unlikely(error))
			return error;
	}
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
		inode_lock_shared(dir_inode);
		dentry = __lookup_slow(&nd->last, nd->path.dentry, nd->flags);
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
	int err;

	nd->last_type = LAST_ROOT;
	nd->flags |= LOOKUP_PARENT;
	if (IS_ERR(name))
		return PTR_ERR(name);
	while (*name == '/')
		name++;
	if (!*name)
		return 0;

	for (;;) {
		struct user_namespace *mnt_userns;
		const char *link;
		u64 hash_len;
		int type;

		mnt_userns = mnt_user_ns(nd->path.mnt);
		/* may_lookup inlined */
		if (nd->flags & LOOKUP_RCU) {
			err = inode_permission(mnt_userns, nd->inode,
					       MAY_EXEC | MAY_NOT_BLOCK);
			if (err != -ECHILD || !try_to_unlazy(nd)) {
				if (err)
					return err;
				goto past_may_lookup;
			}
		}
		err = inode_permission(mnt_userns, nd->inode, MAY_EXEC);
		if (err)
			return err;
past_may_lookup:

		hash_len = hash_name(nd->path.dentry, name);

		type = LAST_NORM;
		if (name[0] == '.')
			switch (hashlen_len(hash_len)) {
			case 2:
				if (name[1] == '.') {
					type = LAST_DOTDOT;
					nd->state |= ND_JUMPED;
				}
				break;
			case 1:
				type = LAST_DOT;
			}
		if (likely(type == LAST_NORM)) {
			nd->state &= ~ND_JUMPED;
			/* DCACHE_OP_HASH check removed - d_hash never set */
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
	nd->state |= ND_JUMPED;

	nd->m_seq = __read_seqcount_begin(&mount_lock.seqcount);
	smp_rmb();

	if (nd->state & ND_ROOT_PRESET) {
		if (*s && unlikely(!d_can_lookup(nd->root.dentry)))
			return ERR_PTR(-ENOTDIR);
		nd->path = nd->root;
		nd->inode = nd->root.dentry->d_inode;
		if (!(flags & LOOKUP_RCU))
			path_get(&nd->path);
		return s;
	}

	nd->root.mnt = NULL;

	/* Simplified: handle absolute/relative paths - LOOKUP_IN_ROOT never set */
	if (*s == '/') {
		error = nd_jump_root(nd);
		if (unlikely(error))
			return ERR_PTR(error);
		return s;
	}

	if (nd->dfd == AT_FDCWD) {
		get_fs_pwd(current->fs, &nd->path);
		nd->inode = nd->path.dentry->d_inode;
	} else {
		struct fd f = fdget_raw(nd->dfd);
		if (!f.file)
			return ERR_PTR(-EBADF);

		if (*s && unlikely(!d_can_lookup(f.file->f_path.dentry))) {
			fdput(f);
			return ERR_PTR(-ENOTDIR);
		}

		nd->path = f.file->f_path;
		path_get(&nd->path);
		nd->inode = nd->path.dentry->d_inode;
		fdput(f);
	}

	return s;
}

static int handle_lookup_down(struct nameidata *nd)
{
	if (!(nd->flags & LOOKUP_RCU))
		dget(nd->path.dentry);
	return PTR_ERR(step_into(nd, WALK_NOFOLLOW, nd->path.dentry, nd->inode,
				 nd->seq));
}

static int path_lookupat(struct nameidata *nd, unsigned flags,
			 struct path *path)
{
	const char *s = path_init(nd, flags);
	int err;

	if (unlikely(flags & LOOKUP_DOWN) && !IS_ERR(s)) {
		err = handle_lookup_down(nd);
		if (unlikely(err < 0))
			s = ERR_PTR(err);
	}

	/* Inlined lookup_last into loop */
	while (!(err = link_path_walk(s, nd))) {
		if (nd->last_type == LAST_NORM && nd->last.name[nd->last.len])
			nd->flags |= LOOKUP_FOLLOW | LOOKUP_DIRECTORY;
		s = walk_component(nd, WALK_TRAILING);
		if (s == NULL)
			break;
	}
	if (!err && unlikely(nd->flags & LOOKUP_MOUNTPOINT)) {
		err = handle_lookup_down(nd);
		nd->state &= ~ND_JUMPED;
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

int filename_lookup(int dfd, struct filename *name, unsigned flags,
		    struct path *path, struct path *root)
{
	int retval;
	struct nameidata nd;
	if (IS_ERR(name))
		return PTR_ERR(name);
	set_nameidata(&nd, dfd, name, root);
	retval = path_lookupat(&nd, flags | LOOKUP_RCU, path);
	if (unlikely(retval == -ECHILD))
		retval = path_lookupat(&nd, flags, path);
	if (unlikely(retval == -ESTALE))
		retval = path_lookupat(&nd, flags | LOOKUP_REVAL, path);

	/* audit_inode - empty stub */
	restore_nameidata();
	return retval;
}

/* path_parentat, filename_parentat removed - only caller was filename_create */

int kern_path(const char *name, unsigned int flags, struct path *path)
{
	struct filename *filename = getname_kernel(name);
	int ret = filename_lookup(AT_FDCWD, filename, flags, path, NULL);

	putname(filename);
	return ret;
}

/* vfs_path_lookup removed - never called */

/* may_create removed - never called (only callers vfs_mknod, vfs_mkdir, etc were removed) */

/* Removed: may_open_dev - always returned true */

static int may_open(struct user_namespace *mnt_userns, const struct path *path,
		    int acc_mode, int flag)
{
	struct dentry *dentry = path->dentry;
	struct inode *inode = dentry->d_inode;
	int error;

	if (!inode)
		return -ENOENT;

	switch (inode->i_mode & S_IFMT) {
	case S_IFDIR:
		if (acc_mode & MAY_WRITE)
			return -EISDIR;
		if (acc_mode & MAY_EXEC)
			return -EACCES;
		break;
	case S_IFBLK:
	case S_IFCHR:
		fallthrough;
	case S_IFIFO:
	case S_IFSOCK:
		if (acc_mode & MAY_EXEC)
			return -EACCES;
		flag &= ~O_TRUNC;
		break;
	case S_IFREG:
		if ((acc_mode & MAY_EXEC) && path_noexec(path))
			return -EACCES;
		break;
	}

	error = inode_permission(mnt_userns, inode, MAY_OPEN | acc_mode);
	if (error)
		return error;

	if (IS_APPEND(inode)) {
		if ((flag & O_ACCMODE) != O_RDONLY && !(flag & O_APPEND))
			return -EPERM;
		if (flag & O_TRUNC)
			return -EPERM;
	}

	if (flag & O_NOATIME && !inode_owner_or_capable(mnt_userns, inode))
		return -EPERM;

	return 0;
}

/* atomic_open removed - i_op->atomic_open is never set (~40 LOC) */

static struct dentry *lookup_open(struct nameidata *nd, struct file *file,
				  const struct open_flags *op)
{
	/* Simplified: basic lookup with minimal revalidation */
	struct user_namespace *mnt_userns;
	struct dentry *dir = nd->path.dentry;
	struct inode *dir_inode = dir->d_inode;
	int open_flag = op->open_flag;
	struct dentry *dentry;
	int error;
	umode_t mode = op->mode;
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

	if (dentry->d_inode)
		return dentry;

	/* Simplified creation logic */
	mnt_userns = mnt_user_ns(nd->path.mnt);
	if (open_flag & O_CREAT) {
		if (!IS_POSIXACL(dir->d_inode))
			mode &= ~current_umask();

		if (d_in_lookup(dentry)) {
			struct dentry *res = dir_inode->i_op->lookup(
				dir_inode, dentry, nd->flags);
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

		if (!dentry->d_inode && dir_inode->i_op->create) {
			file->f_mode |= FMODE_CREATED;
			error = dir_inode->i_op->create(mnt_userns, dir_inode,
							dentry, mode,
							open_flag & O_EXCL);
			if (error) {
				dput(dentry);
				return ERR_PTR(error);
			}
		}
	}

	return dentry;
}

static const char *open_last_lookups(struct nameidata *nd, struct file *file,
				     const struct open_flags *op)
{
	struct dentry *dir = nd->path.dentry;
	int open_flag = op->open_flag;
	bool got_write = false;
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

	if (!(open_flag & O_CREAT)) {
		if (nd->last.name[nd->last.len])
			nd->flags |= LOOKUP_FOLLOW | LOOKUP_DIRECTORY;

		dentry = lookup_fast(nd, &inode, &seq);
		if (IS_ERR(dentry))
			return ERR_CAST(dentry);
		if (likely(dentry))
			goto finish_lookup;

		BUG_ON(nd->flags & LOOKUP_RCU);
	} else {
		if (nd->flags & LOOKUP_RCU) {
			if (!try_to_unlazy(nd))
				return ERR_PTR(-ECHILD);
		}
		/* audit_inode - empty stub */
		if (unlikely(nd->last.name[nd->last.len]))
			return ERR_PTR(-EISDIR);
	}

	if (open_flag & (O_CREAT | O_TRUNC | O_WRONLY | O_RDWR)) {
		got_write = !mnt_want_write(nd->path.mnt);
	}
	if (open_flag & O_CREAT)
		inode_lock(dir->d_inode);
	else
		inode_lock_shared(dir->d_inode);
	dentry = lookup_open(nd, file, op);
	if (open_flag & O_CREAT)
		inode_unlock(dir->d_inode);
	else
		inode_unlock_shared(dir->d_inode);

	if (got_write)
		mnt_drop_write(nd->path.mnt);

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
	struct user_namespace *mnt_userns;
	int open_flag = op->open_flag;
	bool need_truncate;
	int acc_mode;
	int error;

	if (!(file->f_mode & (FMODE_OPENED | FMODE_CREATED))) {
		error = complete_walk(nd);
		if (error)
			return error;
	}
	/* audit_inode - empty stub */
	mnt_userns = mnt_user_ns(nd->path.mnt);
	if (open_flag & O_CREAT) {
		if ((open_flag & O_EXCL) && !(file->f_mode & FMODE_CREATED))
			return -EEXIST;
		if (d_is_dir(nd->path.dentry))
			return -EISDIR;
	}
	if ((nd->flags & LOOKUP_DIRECTORY) && !d_can_lookup(nd->path.dentry))
		return -ENOTDIR;

	need_truncate = false;
	acc_mode = op->acc_mode;
	if (file->f_mode & FMODE_CREATED) {
		open_flag &= ~O_TRUNC;
		acc_mode = 0;
	} else if (d_is_reg(nd->path.dentry) && open_flag & O_TRUNC) {
		error = mnt_want_write(nd->path.mnt);
		if (error)
			return error;
		need_truncate = true;
	}
	error = may_open(mnt_userns, &nd->path, acc_mode, open_flag);
	if (!error && !(file->f_mode & FMODE_OPENED))
		error = vfs_open(&nd->path, file);
	if (!error && need_truncate) {
		struct inode *inode = file->f_path.dentry->d_inode;
		error = get_write_access(inode);
		if (!error) {
			error = do_truncate(mnt_userns, file->f_path.dentry, 0,
					    ATTR_MTIME | ATTR_CTIME | ATTR_OPEN,
					    file);
			put_write_access(inode);
		}
	}
	if (unlikely(error > 0)) {
		WARN_ON(1);
		error = -EINVAL;
	}
	if (need_truncate)
		mnt_drop_write(nd->path.mnt);
	return error;
}

/* Removed: do_tmpfile - always returned -EOPNOTSUPP */

static struct file *path_openat(struct nameidata *nd,
				const struct open_flags *op, unsigned flags)
{
	struct file *file;
	int error;

	file = alloc_empty_file(op->open_flag, current_cred());
	if (IS_ERR(file))
		return file;

	if (unlikely(file->f_flags & __O_TMPFILE)) {
		error = -EOPNOTSUPP;
	} else if (unlikely(file->f_flags & O_PATH)) {
		struct path path;
		error = path_lookupat(nd, flags, &path);
		if (!error) {
			error = vfs_open(&path, file);
			path_put(&path);
		}
	} else {
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
	if (error == -EOPENSTALE) {
		if (flags & LOOKUP_RCU)
			error = -ECHILD;
		else
			error = -ESTALE;
	}
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
	if (unlikely(filp == ERR_PTR(-ESTALE)))
		filp = path_openat(&nd, op, flags | LOOKUP_REVAL);
	restore_nameidata();
	return filp;
}

/* vfs_mknod, vfs_mkdir, do_rmdir, do_unlinkat, vfs_symlink, vfs_link removed
 * - only callers were init_* functions */

/* page_get_link, page_put_link, page_symlink, page_symlink_inode_operations
 * removed - symlinks are never created (ramfs/shmem only use S_IFREG/S_IFDIR) */
