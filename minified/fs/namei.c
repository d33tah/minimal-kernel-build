 

#include <linux/init.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/sched/mm.h>
#include <linux/fsnotify.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/ima.h>
#include <linux/syscalls.h>
#include <linux/mount.h>
#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/device_cgroup.h>
#include <linux/fs_struct.h>

#include <linux/hash.h>
#include <linux/bitops.h>
#include <linux/init_task.h>
#include <linux/uaccess.h>

#include "internal.h"
#include "mount.h"

#define EMBEDDED_NAME_MAX	(PATH_MAX - offsetof(struct filename, iname))

struct filename *
getname_flags(const char __user *filename, int flags, int *empty)
{
	struct filename *result;
	char *kname;
	int len;

	result = audit_reusename(filename);
	if (result)
		return result;

	result = __getname();
	if (unlikely(!result))
		return ERR_PTR(-ENOMEM);

	
	kname = (char *)result->iname;
	result->name = kname;

	len = strncpy_from_user(kname, filename, EMBEDDED_NAME_MAX);
	if (unlikely(len < 0)) {
		__putname(result);
		return ERR_PTR(len);
	}

	
	if (unlikely(len == EMBEDDED_NAME_MAX)) {
		const size_t size = offsetof(struct filename, iname[1]);
		kname = (char *)result;

		
		result = kzalloc(size, GFP_KERNEL);
		if (unlikely(!result)) {
			__putname(kname);
			return ERR_PTR(-ENOMEM);
		}
		result->name = kname;
		len = strncpy_from_user(kname, filename, PATH_MAX);
		if (unlikely(len < 0)) {
			__putname(kname);
			kfree(result);
			return ERR_PTR(len);
		}
		if (unlikely(len == PATH_MAX)) {
			__putname(kname);
			kfree(result);
			return ERR_PTR(-ENAMETOOLONG);
		}
	}

	result->refcnt = 1;
	
	if (unlikely(!len)) {
		if (empty)
			*empty = 1;
		if (!(flags & LOOKUP_EMPTY)) {
			putname(result);
			return ERR_PTR(-ENOENT);
		}
	}

	result->uptr = filename;
	result->aname = NULL;
	audit_getname(result);
	return result;
}

struct filename *
getname_uflags(const char __user *filename, int uflags)
{
	int flags = (uflags & AT_EMPTY_PATH) ? LOOKUP_EMPTY : 0;

	return getname_flags(filename, flags, NULL);
}

struct filename *
getname(const char __user * filename)
{
	return getname_flags(filename, 0, NULL);
}

struct filename *
getname_kernel(const char * filename)
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
	result->aname = NULL;
	result->refcnt = 1;
	audit_getname(result);

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

static int check_acl(struct user_namespace *mnt_userns,
		     struct inode *inode, int mask)
{

	return -EAGAIN;
}

static int acl_permission_check(struct user_namespace *mnt_userns,
				struct inode *inode, int mask)
{
	unsigned int mode = inode->i_mode;
	kuid_t i_uid;

	
	i_uid = i_uid_into_mnt(mnt_userns, inode);
	if (likely(uid_eq(current_fsuid(), i_uid))) {
		mask &= 7;
		mode >>= 6;
		return (mask & ~mode) ? -EACCES : 0;
	}

	
	if (IS_POSIXACL(inode) && (mode & S_IRWXG)) {
		int error = check_acl(mnt_userns, inode, mask);
		if (error != -EAGAIN)
			return error;
	}

	
	mask &= 7;

	
	if (mask & (mode ^ (mode >> 3))) {
		kgid_t kgid = i_gid_into_mnt(mnt_userns, inode);
		if (in_group_p(kgid))
			mode >>= 3;
	}

	
	return (mask & ~mode) ? -EACCES : 0;
}

int generic_permission(struct user_namespace *mnt_userns, struct inode *inode,
		       int mask)
{
	int ret;

	
	ret = acl_permission_check(mnt_userns, inode, mask);
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

static inline int do_inode_permission(struct user_namespace *mnt_userns,
				      struct inode *inode, int mask)
{
	if (unlikely(!(inode->i_opflags & IOP_FASTPERM))) {
		if (likely(inode->i_op->permission))
			return inode->i_op->permission(mnt_userns, inode, mask);

		
		spin_lock(&inode->i_lock);
		inode->i_opflags |= IOP_FASTPERM;
		spin_unlock(&inode->i_lock);
	}
	return generic_permission(mnt_userns, inode, mask);
}

static int sb_permission(struct super_block *sb, struct inode *inode, int mask)
{
	if (unlikely(mask & MAY_WRITE)) {
		umode_t mode = inode->i_mode;

		
		if (sb_rdonly(sb) && (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
			return -EROFS;
	}
	return 0;
}

int inode_permission(struct user_namespace *mnt_userns,
		     struct inode *inode, int mask)
{
	int retval;

	retval = sb_permission(inode->i_sb, inode, mask);
	if (retval)
		return retval;

	if (unlikely(mask & MAY_WRITE)) {
		
		if (IS_IMMUTABLE(inode))
			return -EPERM;

		
		if (HAS_UNMAPPED_ID(mnt_userns, inode))
			return -EACCES;
	}

	retval = do_inode_permission(mnt_userns, inode, mask);
	if (retval)
		return retval;

	retval = devcgroup_inode_permission(inode, mask);
	if (retval)
		return retval;

	return security_inode_permission(inode, mask);
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
	struct path	path;
	struct qstr	last;
	struct path	root;
	struct inode	*inode; 
	unsigned int	flags, state;
	unsigned	seq, m_seq, r_seq;
	int		last_type;
	unsigned	depth;
	int		total_link_count;
	struct saved {
		struct path link;
		struct delayed_call done;
		const char *name;
		unsigned seq;
	} *stack, internal[EMBEDDED_LEVELS];
	struct filename	*name;
	struct nameidata *saved;
	unsigned	root_seq;
	int		dfd;
	kuid_t		dir_uid;
	umode_t		dir_mode;
} __randomize_layout;

#define ND_ROOT_PRESET 1
#define ND_ROOT_GRABBED 2
#define ND_JUMPED 4

static void __set_nameidata(struct nameidata *p, int dfd, struct filename *name)
{
	struct nameidata *old = current->nameidata;
	p->stack = p->internal;
	p->depth = 0;
	p->dfd = dfd;
	p->name = name;
	p->path.mnt = NULL;
	p->path.dentry = NULL;
	p->total_link_count = old ? old->total_link_count : 0;
	p->saved = old;
	current->nameidata = p;
}

static inline void set_nameidata(struct nameidata *p, int dfd, struct filename *name,
			  const struct path *root)
{
	__set_nameidata(p, dfd, name);
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
	if (old)
		old->total_link_count = now->total_link_count;
	if (now->stack != now->internal)
		kfree(now->stack);
}

static bool nd_alloc_stack(struct nameidata *nd)
{
	struct saved *p;

	p= kmalloc_array(MAXSYMLINKS, sizeof(struct saved),
			 nd->flags & LOOKUP_RCU ? GFP_ATOMIC : GFP_KERNEL);
	if (unlikely(!p))
		return false;
	memcpy(p, nd->internal, sizeof(nd->internal));
	nd->stack = p;
	return true;
}

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

static bool __legitimize_path(struct path *path, unsigned seq, unsigned mseq)
{
	int res = __legitimize_mnt(path->mnt, mseq);
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

static inline bool legitimize_path(struct nameidata *nd,
			    struct path *path, unsigned seq)
{
	return __legitimize_path(path, seq, nd->m_seq);
}

static bool legitimize_links(struct nameidata *nd)
{
	int i;
	if (unlikely(nd->flags & LOOKUP_CACHED)) {
		drop_links(nd);
		nd->depth = 0;
		return false;
	}
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

static bool try_to_unlazy_next(struct nameidata *nd, struct dentry *dentry, unsigned seq)
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

static inline int d_revalidate(struct dentry *dentry, unsigned int flags)
{
	if (unlikely(dentry->d_flags & DCACHE_OP_REVALIDATE))
		return dentry->d_op->d_revalidate(dentry, flags);
	else
		return 1;
}

static int complete_walk(struct nameidata *nd)
{
	struct dentry *dentry = nd->path.dentry;
	int status;

	if (nd->flags & LOOKUP_RCU) {
		
		if (!(nd->state & ND_ROOT_PRESET))
			if (!(nd->flags & LOOKUP_IS_SCOPED))
				nd->root.mnt = NULL;
		nd->flags &= ~LOOKUP_CACHED;
		if (!try_to_unlazy(nd))
			return -ECHILD;
	}

	if (unlikely(nd->flags & LOOKUP_IS_SCOPED)) {
		
		if (!path_is_under(&nd->path, &nd->root))
			return -EXDEV;
	}

	if (likely(!(nd->state & ND_JUMPED)))
		return 0;

	if (likely(!(dentry->d_flags & DCACHE_OP_WEAK_REVALIDATE)))
		return 0;

	status = dentry->d_op->d_weak_revalidate(dentry, nd->flags);
	if (status > 0)
		return 0;

	if (!status)
		status = -ESTALE;

	return status;
}

static int set_root(struct nameidata *nd)
{
	struct fs_struct *fs = current->fs;

	
	if (WARN_ON(nd->flags & LOOKUP_IS_SCOPED))
		return -ENOTRECOVERABLE;

	if (nd->flags & LOOKUP_RCU) {
		unsigned seq;

		do {
			seq = read_seqcount_begin(&fs->seq);
			nd->root = fs->root;
			nd->root_seq = __read_seqcount_begin(&nd->root.dentry->d_seq);
		} while (read_seqcount_retry(&fs->seq, seq));
	} else {
		get_fs_root(fs, &nd->root);
		nd->state |= ND_ROOT_GRABBED;
	}
	return 0;
}

static int nd_jump_root(struct nameidata *nd)
{
	if (unlikely(nd->flags & LOOKUP_BENEATH))
		return -EXDEV;
	if (unlikely(nd->flags & LOOKUP_NO_XDEV)) {
		
		if (nd->path.mnt != NULL && nd->path.mnt != nd->root.mnt)
			return -EXDEV;
	}
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

int nd_jump_link(struct path *path)
{
	int error = -ELOOP;
	struct nameidata *nd = current->nameidata;

	if (unlikely(nd->flags & LOOKUP_NO_MAGICLINKS))
		goto err;

	error = -EXDEV;
	if (unlikely(nd->flags & LOOKUP_NO_XDEV)) {
		if (nd->path.mnt != path->mnt)
			goto err;
	}
	
	if (unlikely(nd->flags & LOOKUP_IS_SCOPED))
		goto err;

	path_put(&nd->path);
	nd->path = *path;
	nd->inode = nd->path.dentry->d_inode;
	nd->state |= ND_JUMPED;
	return 0;

err:
	path_put(path);
	return error;
}

static inline void put_link(struct nameidata *nd)
{
	struct saved *last = nd->stack + --nd->depth;
	do_delayed_call(&last->done);
	if (!(nd->flags & LOOKUP_RCU))
		path_put(&last->link);
}

static int sysctl_protected_symlinks __read_mostly;
static int sysctl_protected_hardlinks __read_mostly;

static inline int may_follow_link(struct nameidata *nd, const struct inode *inode)
{
	/* Stub: allow following links */
	return 0;
}

static bool safe_hardlink_source(struct user_namespace *mnt_userns,
				 struct inode *inode)
{
	/* Stub: allow hardlinks */
	return true;
}

int may_linkat(struct user_namespace *mnt_userns, struct path *link)
{
	/* Stub: allow hardlinks */
	return 0;
}

static int may_create_in_sticky(struct user_namespace *mnt_userns,
				struct nameidata *nd, struct inode *const inode)
{
	
	return 0;
}

int follow_up(struct path *path)
{
	return 0;
}

static bool choose_mountpoint_rcu(struct mount *m, const struct path *root,
				  struct path *path, unsigned *seqp)
{
	while (mnt_has_parent(m)) {
		struct dentry *mountpoint = m->mnt_mountpoint;

		m = m->mnt_parent;
		if (unlikely(root->dentry == mountpoint &&
			     root->mnt == &m->mnt))
			break;
		if (mountpoint != m->mnt.mnt_root) {
			path->mnt = &m->mnt;
			path->dentry = mountpoint;
			*seqp = read_seqcount_begin(&mountpoint->d_seq);
			return true;
		}
	}
	return false;
}

static bool choose_mountpoint(struct mount *m, const struct path *root,
			      struct path *path)
{
	bool found;

	rcu_read_lock();
	while (1) {
		unsigned seq, mseq = read_seqbegin(&mount_lock);

		found = choose_mountpoint_rcu(m, root, path, &seq);
		if (unlikely(!found)) {
			if (!read_seqretry(&mount_lock, mseq))
				break;
		} else {
			if (likely(__legitimize_path(path, seq, mseq)))
				break;
			rcu_read_unlock();
			path_put(path);
			rcu_read_lock();
		}
	}
	rcu_read_unlock();
	return found;
}

static int follow_automount(struct path *path, int *count, unsigned lookup_flags)
{
	return -EISDIR; /* Stub */
}

static int __traverse_mounts(struct path *path, unsigned flags, bool *jumped,
			     int *count, unsigned lookup_flags)
{
	*jumped = false;
	return 0; /* Stub */
}

static inline int traverse_mounts(struct path *path, bool *jumped,
				  int *count, unsigned lookup_flags)
{
	unsigned flags = smp_load_acquire(&path->dentry->d_flags);

	
	if (likely(!(flags & DCACHE_MANAGED_DENTRY))) {
		*jumped = false;
		if (unlikely(d_flags_negative(flags)))
			return -ENOENT;
		return 0;
	}
	return __traverse_mounts(path, flags, jumped, count, lookup_flags);
}

int follow_down_one(struct path *path)
{
	struct vfsmount *mounted;

	mounted = lookup_mnt(path);
	if (mounted) {
		dput(path->dentry);
		mntput(path->mnt);
		path->mnt = mounted;
		path->dentry = dget(mounted->mnt_root);
		return 1;
	}
	return 0;
}

int follow_down(struct path *path)
{
	struct vfsmount *mnt = path->mnt;
	bool jumped;
	int ret = traverse_mounts(path, &jumped, NULL, 0);

	if (path->mnt != mnt)
		mntput(mnt);
	return ret;
}

static bool __follow_mount_rcu(struct nameidata *nd, struct path *path,
			       struct inode **inode, unsigned *seqp)
{
	struct dentry *dentry = path->dentry;
	unsigned int flags = dentry->d_flags;

	if (likely(!(flags & DCACHE_MANAGED_DENTRY)))
		return true;

	if (unlikely(nd->flags & LOOKUP_NO_XDEV))
		return false;

	for (;;) {
		
		if (unlikely(flags & DCACHE_MANAGE_TRANSIT)) {
			int res = dentry->d_op->d_manage(path, true);
			if (res)
				return res == -EISDIR;
			flags = dentry->d_flags;
		}

		if (flags & DCACHE_MOUNTED) {
			struct mount *mounted = __lookup_mnt(path->mnt, dentry);
			if (mounted) {
				path->mnt = &mounted->mnt;
				dentry = path->dentry = mounted->mnt.mnt_root;
				nd->state |= ND_JUMPED;
				*seqp = read_seqcount_begin(&dentry->d_seq);
				*inode = dentry->d_inode;
				
				flags = dentry->d_flags;
				continue;
			}
			if (read_seqretry(&mount_lock, nd->m_seq))
				return false;
		}
		return !(flags & DCACHE_NEED_AUTOMOUNT);
	}
}

static inline int handle_mounts(struct nameidata *nd, struct dentry *dentry,
			  struct path *path, struct inode **inode,
			  unsigned int *seqp)
{
	bool jumped;
	int ret;

	path->mnt = nd->path.mnt;
	path->dentry = dentry;
	if (nd->flags & LOOKUP_RCU) {
		unsigned int seq = *seqp;
		if (unlikely(!*inode))
			return -ENOENT;
		if (likely(__follow_mount_rcu(nd, path, inode, seqp)))
			return 0;
		if (!try_to_unlazy_next(nd, dentry, seq))
			return -ECHILD;
		 
		path->mnt = nd->path.mnt;
		path->dentry = dentry;
	}
	ret = traverse_mounts(path, &jumped, &nd->total_link_count, nd->flags);
	if (jumped) {
		if (unlikely(nd->flags & LOOKUP_NO_XDEV))
			ret = -EXDEV;
		else
			nd->state |= ND_JUMPED;
	}
	if (unlikely(ret)) {
		dput(path->dentry);
		if (path->mnt != nd->path.mnt)
			mntput(path->mnt);
	} else {
		*inode = d_backing_inode(path->dentry);
		*seqp = 0; 
	}
	return ret;
}

static struct dentry *lookup_dcache(const struct qstr *name,
				    struct dentry *dir,
				    unsigned int flags)
{
	struct dentry *dentry = d_lookup(dir, name);
	if (dentry) {
		int error = d_revalidate(dentry, flags);
		if (unlikely(error <= 0)) {
			if (!error)
				d_invalidate(dentry);
			dput(dentry);
			return ERR_PTR(error);
		}
	}
	return dentry;
}

static struct dentry *__lookup_hash(const struct qstr *name,
		struct dentry *base, unsigned int flags)
{
	struct dentry *dentry = lookup_dcache(name, base, flags);
	struct dentry *old;
	struct inode *dir = base->d_inode;

	if (dentry)
		return dentry;

	
	if (unlikely(IS_DEADDIR(dir)))
		return ERR_PTR(-ENOENT);

	dentry = d_alloc(base, name);
	if (unlikely(!dentry))
		return ERR_PTR(-ENOMEM);

	old = dir->i_op->lookup(dir, dentry, flags);
	if (unlikely(old)) {
		dput(dentry);
		dentry = old;
	}
	return dentry;
}

static struct dentry *lookup_fast(struct nameidata *nd,
				  struct inode **inode,
			          unsigned *seqp)
{
	struct dentry *dentry, *parent = nd->path.dentry;
	int status = 1;

	
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
		status = d_revalidate(dentry, nd->flags);
		if (likely(status > 0))
			return dentry;
		if (!try_to_unlazy_next(nd, dentry, seq))
			return ERR_PTR(-ECHILD);
		if (status == -ECHILD)
			
			status = d_revalidate(dentry, nd->flags);
	} else {
		dentry = __d_lookup(parent, &nd->last);
		if (unlikely(!dentry))
			return NULL;
		status = d_revalidate(dentry, nd->flags);
	}
	if (unlikely(status <= 0)) {
		if (!status)
			d_invalidate(dentry);
		dput(dentry);
		return ERR_PTR(status);
	}
	return dentry;
}

static struct dentry *__lookup_slow(const struct qstr *name,
				    struct dentry *dir,
				    unsigned int flags)
{
	struct dentry *dentry, *old;
	struct inode *inode = dir->d_inode;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);

	
	if (unlikely(IS_DEADDIR(inode)))
		return ERR_PTR(-ENOENT);
again:
	dentry = d_alloc_parallel(dir, name, &wq);
	if (IS_ERR(dentry))
		return dentry;
	if (unlikely(!d_in_lookup(dentry))) {
		int error = d_revalidate(dentry, flags);
		if (unlikely(error <= 0)) {
			if (!error) {
				d_invalidate(dentry);
				dput(dentry);
				goto again;
			}
			dput(dentry);
			dentry = ERR_PTR(error);
		}
	} else {
		old = inode->i_op->lookup(inode, dentry, flags);
		d_lookup_done(dentry);
		if (unlikely(old)) {
			dput(dentry);
			dentry = old;
		}
	}
	return dentry;
}

static struct dentry *lookup_slow(const struct qstr *name,
				  struct dentry *dir,
				  unsigned int flags)
{
	struct inode *inode = dir->d_inode;
	struct dentry *res;
	inode_lock_shared(inode);
	res = __lookup_slow(name, dir, flags);
	inode_unlock_shared(inode);
	return res;
}

static inline int may_lookup(struct user_namespace *mnt_userns,
			     struct nameidata *nd)
{
	if (nd->flags & LOOKUP_RCU) {
		int err = inode_permission(mnt_userns, nd->inode, MAY_EXEC|MAY_NOT_BLOCK);
		if (err != -ECHILD || !try_to_unlazy(nd))
			return err;
	}
	return inode_permission(mnt_userns, nd->inode, MAY_EXEC);
}

static int reserve_stack(struct nameidata *nd, struct path *link, unsigned seq)
{
	if (unlikely(nd->total_link_count++ >= MAXSYMLINKS))
		return -ELOOP;

	if (likely(nd->depth != EMBEDDED_LEVELS))
		return 0;
	if (likely(nd->stack != nd->internal))
		return 0;
	if (likely(nd_alloc_stack(nd)))
		return 0;

	if (nd->flags & LOOKUP_RCU) {
		 
		 
		bool grabbed_link = legitimize_path(nd, link, seq);

		if (!try_to_unlazy(nd) || !grabbed_link)
			return -ECHILD;

		if (nd_alloc_stack(nd))
			return 0;
	}
	return -ENOMEM;
}

enum {WALK_TRAILING = 1, WALK_MORE = 2, WALK_NOFOLLOW = 4};

static const char *pick_link(struct nameidata *nd, struct path *link,
		     struct inode *inode, unsigned seq, int flags)
{
	struct saved *last;
	const char *res;
	int error = reserve_stack(nd, link, seq);

	if (unlikely(error)) {
		if (!(nd->flags & LOOKUP_RCU))
			path_put(link);
		return ERR_PTR(error);
	}
	last = nd->stack + nd->depth++;
	last->link = *link;
	clear_delayed_call(&last->done);
	last->seq = seq;

	if (flags & WALK_TRAILING) {
		error = may_follow_link(nd, inode);
		if (unlikely(error))
			return ERR_PTR(error);
	}

	if (unlikely(nd->flags & LOOKUP_NO_SYMLINKS) ||
			unlikely(link->mnt->mnt_flags & MNT_NOSYMFOLLOW))
		return ERR_PTR(-ELOOP);

	if (!(nd->flags & LOOKUP_RCU)) {
		touch_atime(&last->link);
		cond_resched();
	} else if (atime_needs_update(&last->link, inode)) {
		if (!try_to_unlazy(nd))
			return ERR_PTR(-ECHILD);
		touch_atime(&last->link);
	}

	error = security_inode_follow_link(link->dentry, inode,
					   nd->flags & LOOKUP_RCU);
	if (unlikely(error))
		return ERR_PTR(error);

	res = READ_ONCE(inode->i_link);
	if (!res) {
		const char * (*get)(struct dentry *, struct inode *,
				struct delayed_call *);
		get = inode->i_op->get_link;
		if (nd->flags & LOOKUP_RCU) {
			res = get(NULL, inode, &last->done);
			if (res == ERR_PTR(-ECHILD) && try_to_unlazy(nd))
				res = get(link->dentry, inode, &last->done);
		} else {
			res = get(link->dentry, inode, &last->done);
		}
		if (!res)
			goto all_done;
		if (IS_ERR(res))
			return res;
	}
	if (*res == '/') {
		error = nd_jump_root(nd);
		if (unlikely(error))
			return ERR_PTR(error);
		while (unlikely(*++res == '/'))
			;
	}
	if (*res)
		return res;
all_done:  
	put_link(nd);
	return NULL;
}

static const char *step_into(struct nameidata *nd, int flags,
		     struct dentry *dentry, struct inode *inode, unsigned seq)
{
	struct path path;
	int err = handle_mounts(nd, dentry, &path, &inode, &seq);

	if (err < 0)
		return ERR_PTR(err);
	if (likely(!d_is_symlink(path.dentry)) ||
	   ((flags & WALK_TRAILING) && !(nd->flags & LOOKUP_FOLLOW)) ||
	   (flags & WALK_NOFOLLOW)) {
		
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
	if (nd->flags & LOOKUP_RCU) {
		
		if (read_seqcount_retry(&path.dentry->d_seq, seq))
			return ERR_PTR(-ECHILD);
	} else {
		if (path.mnt == nd->path.mnt)
			mntget(path.mnt);
	}
	return pick_link(nd, &path, inode, seq, flags);
}

static struct dentry *follow_dotdot_rcu(struct nameidata *nd,
					struct inode **inodep,
					unsigned *seqp)
{
	struct dentry *parent, *old;

	if (path_equal(&nd->path, &nd->root))
		goto in_root;
	if (unlikely(nd->path.dentry == nd->path.mnt->mnt_root)) {
		struct path path;
		unsigned seq;
		if (!choose_mountpoint_rcu(real_mount(nd->path.mnt),
					   &nd->root, &path, &seq))
			goto in_root;
		if (unlikely(nd->flags & LOOKUP_NO_XDEV))
			return ERR_PTR(-ECHILD);
		nd->path = path;
		nd->inode = path.dentry->d_inode;
		nd->seq = seq;
		if (unlikely(read_seqretry(&mount_lock, nd->m_seq)))
			return ERR_PTR(-ECHILD);
		
	}
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
	if (unlikely(nd->flags & LOOKUP_BENEATH))
		return ERR_PTR(-ECHILD);
	return NULL;
}

static struct dentry *follow_dotdot(struct nameidata *nd,
				 struct inode **inodep,
				 unsigned *seqp)
{
	struct dentry *parent;

	if (path_equal(&nd->path, &nd->root))
		goto in_root;
	if (unlikely(nd->path.dentry == nd->path.mnt->mnt_root)) {
		struct path path;

		if (!choose_mountpoint(real_mount(nd->path.mnt),
				       &nd->root, &path))
			goto in_root;
		path_put(&nd->path);
		nd->path = path;
		nd->inode = path.dentry->d_inode;
		if (unlikely(nd->flags & LOOKUP_NO_XDEV))
			return ERR_PTR(-EXDEV);
	}
	
	parent = dget_parent(nd->path.dentry);
	if (unlikely(!path_connected(nd->path.mnt, parent))) {
		dput(parent);
		return ERR_PTR(-ENOENT);
	}
	*seqp = 0;
	*inodep = parent->d_inode;
	return parent;

in_root:
	if (unlikely(nd->flags & LOOKUP_BENEATH))
		return ERR_PTR(-EXDEV);
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
			error = step_into(nd, WALK_NOFOLLOW,
					 nd->path.dentry, nd->inode, nd->seq);
		else
			error = step_into(nd, WALK_NOFOLLOW,
					 parent, inode, seq);
		if (unlikely(error))
			return error;

		if (unlikely(nd->flags & LOOKUP_IS_SCOPED)) {
			
			smp_rmb();
			if (unlikely(__read_seqcount_retry(&mount_lock.seqcount, nd->m_seq)))
				return ERR_PTR(-EAGAIN);
			if (unlikely(__read_seqcount_retry(&rename_lock.seqcount, nd->r_seq)))
				return ERR_PTR(-EAGAIN);
		}
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
		dentry = lookup_slow(&nd->last, nd->path.dentry, nd->flags);
		if (IS_ERR(dentry))
			return ERR_CAST(dentry);
	}
	if (!(flags & WALK_MORE) && nd->depth)
		put_link(nd);
	return step_into(nd, flags, dentry, inode, seq);
}

#include <asm/word-at-a-time.h>

#ifdef HASH_MIX

#else	

#define HASH_MIX(x, y, a)	\
	(	x ^= (a),	\
	y ^= x,	x = rol32(x, 7),\
	x += y,	y = rol32(y,20),\
	y *= 9			)

static inline unsigned int fold_hash(unsigned long x, unsigned long y)
{
	
	return __hash_32(y ^ __hash_32(x));
}

#endif

unsigned int full_name_hash(const void *salt, const char *name, unsigned int len)
{
	unsigned long a, x = 0, y = (unsigned long)salt;

	for (;;) {
		if (!len)
			goto done;
		a = load_unaligned_zeropad(name);
		if (len < sizeof(unsigned long))
			break;
		HASH_MIX(x, y, a);
		name += sizeof(unsigned long);
		len -= sizeof(unsigned long);
	}
	x ^= a & bytemask_from_count(len);
done:
	return fold_hash(x, y);
}

u64 hashlen_string(const void *salt, const char *name)
{
	unsigned long a = 0, x = 0, y = (unsigned long)salt;
	unsigned long adata, mask, len;
	const struct word_at_a_time constants = WORD_AT_A_TIME_CONSTANTS;

	len = 0;
	goto inside;

	do {
		HASH_MIX(x, y, a);
		len += sizeof(unsigned long);
inside:
		a = load_unaligned_zeropad(name+len);
	} while (!has_zero(a, &adata, &constants));

	adata = prep_zero_mask(a, adata, &constants);
	mask = create_zero_mask(adata);
	x ^= a & zero_bytemask(mask);

	return hashlen_create(fold_hash(x, y), len + find_zero(mask));
}

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
		a = load_unaligned_zeropad(name+len);
		b = a ^ REPEAT_BYTE('/');
	} while (!(has_zero(a, &adata, &constants) | has_zero(b, &bdata, &constants)));

	adata = prep_zero_mask(a, adata, &constants);
	bdata = prep_zero_mask(b, bdata, &constants);
	mask = create_zero_mask(adata | bdata);
	x ^= a & zero_bytemask(mask);

	return hashlen_create(fold_hash(x, y), len + find_zero(mask));
}

static int link_path_walk(const char *name, struct nameidata *nd)
{
	int depth = 0;  
	int err;

	nd->last_type = LAST_ROOT;
	nd->flags |= LOOKUP_PARENT;
	if (IS_ERR(name))
		return PTR_ERR(name);
	while (*name=='/')
		name++;
	if (!*name) {
		nd->dir_mode = 0;  
		return 0;
	}

	
	for(;;) {
		struct user_namespace *mnt_userns;
		const char *link;
		u64 hash_len;
		int type;

		mnt_userns = mnt_user_ns(nd->path.mnt);
		err = may_lookup(mnt_userns, nd);
		if (err)
			return err;

		hash_len = hash_name(nd->path.dentry, name);

		type = LAST_NORM;
		if (name[0] == '.') switch (hashlen_len(hash_len)) {
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
			struct dentry *parent = nd->path.dentry;
			nd->state &= ~ND_JUMPED;
			if (unlikely(parent->d_flags & DCACHE_OP_HASH)) {
				struct qstr this = { { .hash_len = hash_len }, .name = name };
				err = parent->d_op->d_hash(parent, &this);
				if (err < 0)
					return err;
				hash_len = this.hash_len;
				name = this.name;
			}
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
				nd->dir_uid = i_uid_into_mnt(mnt_userns, nd->inode);
				nd->dir_mode = nd->inode->i_mode;
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

	
	if ((flags & (LOOKUP_RCU | LOOKUP_CACHED)) == LOOKUP_CACHED)
		return ERR_PTR(-EAGAIN);

	if (!*s)
		flags &= ~LOOKUP_RCU;
	if (flags & LOOKUP_RCU)
		rcu_read_lock();

	nd->flags = flags;
	nd->state |= ND_JUMPED;

	nd->m_seq = __read_seqcount_begin(&mount_lock.seqcount);
	nd->r_seq = __read_seqcount_begin(&rename_lock.seqcount);
	smp_rmb();

	if (nd->state & ND_ROOT_PRESET) {
		struct dentry *root = nd->root.dentry;
		struct inode *inode = root->d_inode;
		if (*s && unlikely(!d_can_lookup(root)))
			return ERR_PTR(-ENOTDIR);
		nd->path = nd->root;
		nd->inode = inode;
		if (flags & LOOKUP_RCU) {
			nd->seq = read_seqcount_begin(&nd->path.dentry->d_seq);
			nd->root_seq = nd->seq;
		} else {
			path_get(&nd->path);
		}
		return s;
	}

	nd->root.mnt = NULL;

	
	if (*s == '/' && !(flags & LOOKUP_IN_ROOT)) {
		error = nd_jump_root(nd);
		if (unlikely(error))
			return ERR_PTR(error);
		return s;
	}

	
	if (nd->dfd == AT_FDCWD) {
		if (flags & LOOKUP_RCU) {
			struct fs_struct *fs = current->fs;
			unsigned seq;

			do {
				seq = read_seqcount_begin(&fs->seq);
				nd->path = fs->pwd;
				nd->inode = nd->path.dentry->d_inode;
				nd->seq = __read_seqcount_begin(&nd->path.dentry->d_seq);
			} while (read_seqcount_retry(&fs->seq, seq));
		} else {
			get_fs_pwd(current->fs, &nd->path);
			nd->inode = nd->path.dentry->d_inode;
		}
	} else {
		
		struct fd f = fdget_raw(nd->dfd);
		struct dentry *dentry;

		if (!f.file)
			return ERR_PTR(-EBADF);

		dentry = f.file->f_path.dentry;

		if (*s && unlikely(!d_can_lookup(dentry))) {
			fdput(f);
			return ERR_PTR(-ENOTDIR);
		}

		nd->path = f.file->f_path;
		if (flags & LOOKUP_RCU) {
			nd->inode = nd->path.dentry->d_inode;
			nd->seq = read_seqcount_begin(&nd->path.dentry->d_seq);
		} else {
			path_get(&nd->path);
			nd->inode = nd->path.dentry->d_inode;
		}
		fdput(f);
	}

	
	if (flags & LOOKUP_IS_SCOPED) {
		nd->root = nd->path;
		if (flags & LOOKUP_RCU) {
			nd->root_seq = nd->seq;
		} else {
			path_get(&nd->root);
			nd->state |= ND_ROOT_GRABBED;
		}
	}
	return s;
}

static inline const char *lookup_last(struct nameidata *nd)
{
	if (nd->last_type == LAST_NORM && nd->last.name[nd->last.len])
		nd->flags |= LOOKUP_FOLLOW | LOOKUP_DIRECTORY;

	return walk_component(nd, WALK_TRAILING);
}

static int handle_lookup_down(struct nameidata *nd)
{
	if (!(nd->flags & LOOKUP_RCU))
		dget(nd->path.dentry);
	return PTR_ERR(step_into(nd, WALK_NOFOLLOW,
			nd->path.dentry, nd->inode, nd->seq));
}

static int path_lookupat(struct nameidata *nd, unsigned flags, struct path *path)
{
	const char *s = path_init(nd, flags);
	int err;

	if (unlikely(flags & LOOKUP_DOWN) && !IS_ERR(s)) {
		err = handle_lookup_down(nd);
		if (unlikely(err < 0))
			s = ERR_PTR(err);
	}

	while (!(err = link_path_walk(s, nd)) &&
	       (s = lookup_last(nd)) != NULL)
		;
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

	if (likely(!retval))
		audit_inode(name, path->dentry,
			    flags & LOOKUP_MOUNTPOINT ? AUDIT_INODE_NOEVAL : 0);
	restore_nameidata();
	return retval;
}

static int path_parentat(struct nameidata *nd, unsigned flags,
				struct path *parent)
{
	const char *s = path_init(nd, flags);
	int err = link_path_walk(s, nd);
	if (!err)
		err = complete_walk(nd);
	if (!err) {
		*parent = nd->path;
		nd->path.mnt = NULL;
		nd->path.dentry = NULL;
	}
	terminate_walk(nd);
	return err;
}

static int filename_parentat(int dfd, struct filename *name,
			     unsigned int flags, struct path *parent,
			     struct qstr *last, int *type)
{
	int retval;
	struct nameidata nd;

	if (IS_ERR(name))
		return PTR_ERR(name);
	set_nameidata(&nd, dfd, name, NULL);
	retval = path_parentat(&nd, flags | LOOKUP_RCU, parent);
	if (unlikely(retval == -ECHILD))
		retval = path_parentat(&nd, flags, parent);
	if (unlikely(retval == -ESTALE))
		retval = path_parentat(&nd, flags | LOOKUP_REVAL, parent);
	if (likely(!retval)) {
		*last = nd.last;
		*type = nd.last_type;
		audit_inode(name, parent->dentry, AUDIT_INODE_PARENT);
	}
	restore_nameidata();
	return retval;
}

static struct dentry *__kern_path_locked(struct filename *name, struct path *path)
{
	struct dentry *d;
	struct qstr last;
	int type, error;

	error = filename_parentat(AT_FDCWD, name, 0, path, &last, &type);
	if (error)
		return ERR_PTR(error);
	if (unlikely(type != LAST_NORM)) {
		path_put(path);
		return ERR_PTR(-EINVAL);
	}
	inode_lock_nested(path->dentry->d_inode, I_MUTEX_PARENT);
	d = __lookup_hash(&last, path->dentry, 0);
	if (IS_ERR(d)) {
		inode_unlock(path->dentry->d_inode);
		path_put(path);
	}
	return d;
}

struct dentry *kern_path_locked(const char *name, struct path *path)
{
	struct filename *filename = getname_kernel(name);
	struct dentry *res = __kern_path_locked(filename, path);

	putname(filename);
	return res;
}

int kern_path(const char *name, unsigned int flags, struct path *path)
{
	struct filename *filename = getname_kernel(name);
	int ret = filename_lookup(AT_FDCWD, filename, flags, path, NULL);

	putname(filename);
	return ret;

}

int vfs_path_lookup(struct dentry *dentry, struct vfsmount *mnt,
		    const char *name, unsigned int flags,
		    struct path *path)
{
	struct filename *filename;
	struct path root = {.mnt = mnt, .dentry = dentry};
	int ret;

	filename = getname_kernel(name);
	
	ret = filename_lookup(AT_FDCWD, filename, flags, path, &root);
	putname(filename);
	return ret;
}

static int lookup_one_common(struct user_namespace *mnt_userns,
			     const char *name, struct dentry *base, int len,
			     struct qstr *this)
{
	this->name = name;
	this->len = len;
	this->hash = full_name_hash(base, name, len);
	if (!len)
		return -EACCES;

	if (unlikely(name[0] == '.')) {
		if (len < 2 || (len == 2 && name[1] == '.'))
			return -EACCES;
	}

	while (len--) {
		unsigned int c = *(const unsigned char *)name++;
		if (c == '/' || c == '\0')
			return -EACCES;
	}
	
	if (base->d_flags & DCACHE_OP_HASH) {
		int err = base->d_op->d_hash(base, this);
		if (err < 0)
			return err;
	}

	return inode_permission(mnt_userns, base->d_inode, MAY_EXEC);
}

struct dentry *try_lookup_one_len(const char *name, struct dentry *base, int len)
{
	struct qstr this;
	int err;

	WARN_ON_ONCE(!inode_is_locked(base->d_inode));

	err = lookup_one_common(&init_user_ns, name, base, len, &this);
	if (err)
		return ERR_PTR(err);

	return lookup_dcache(&this, base, 0);
}

struct dentry *lookup_one_len(const char *name, struct dentry *base, int len)
{
	struct dentry *dentry;
	struct qstr this;
	int err;

	WARN_ON_ONCE(!inode_is_locked(base->d_inode));

	err = lookup_one_common(&init_user_ns, name, base, len, &this);
	if (err)
		return ERR_PTR(err);

	dentry = lookup_dcache(&this, base, 0);
	return dentry ? dentry : __lookup_slow(&this, base, 0);
}

struct dentry *lookup_one(struct user_namespace *mnt_userns, const char *name,
			  struct dentry *base, int len)
{
	struct dentry *dentry;
	struct qstr this;
	int err;

	WARN_ON_ONCE(!inode_is_locked(base->d_inode));

	err = lookup_one_common(mnt_userns, name, base, len, &this);
	if (err)
		return ERR_PTR(err);

	dentry = lookup_dcache(&this, base, 0);
	return dentry ? dentry : __lookup_slow(&this, base, 0);
}

struct dentry *lookup_one_unlocked(struct user_namespace *mnt_userns,
				   const char *name, struct dentry *base,
				   int len)
{
	struct qstr this;
	int err;
	struct dentry *ret;

	err = lookup_one_common(mnt_userns, name, base, len, &this);
	if (err)
		return ERR_PTR(err);

	ret = lookup_dcache(&this, base, 0);
	if (!ret)
		ret = lookup_slow(&this, base, 0);
	return ret;
}

struct dentry *lookup_one_positive_unlocked(struct user_namespace *mnt_userns,
					    const char *name,
					    struct dentry *base, int len)
{
	struct dentry *ret = lookup_one_unlocked(mnt_userns, name, base, len);

	if (!IS_ERR(ret) && d_flags_negative(smp_load_acquire(&ret->d_flags))) {
		dput(ret);
		ret = ERR_PTR(-ENOENT);
	}
	return ret;
}

struct dentry *lookup_one_len_unlocked(const char *name,
				       struct dentry *base, int len)
{
	return lookup_one_unlocked(&init_user_ns, name, base, len);
}

struct dentry *lookup_positive_unlocked(const char *name,
				       struct dentry *base, int len)
{
	return lookup_one_positive_unlocked(&init_user_ns, name, base, len);
}

int user_path_at_empty(int dfd, const char __user *name, unsigned flags,
		 struct path *path, int *empty)
{
	struct filename *filename = getname_flags(name, flags, empty);
	int ret = filename_lookup(dfd, filename, flags, path, NULL);

	putname(filename);
	return ret;
}

int __check_sticky(struct user_namespace *mnt_userns, struct inode *dir,
		   struct inode *inode)
{
	kuid_t fsuid = current_fsuid();

	if (uid_eq(i_uid_into_mnt(mnt_userns, inode), fsuid))
		return 0;
	if (uid_eq(i_uid_into_mnt(mnt_userns, dir), fsuid))
		return 0;
	return !capable_wrt_inode_uidgid(mnt_userns, inode, CAP_FOWNER);
}

static int may_delete(struct user_namespace *mnt_userns, struct inode *dir,
		      struct dentry *victim, bool isdir)
{
	struct inode *inode = d_backing_inode(victim);
	int error;

	if (d_is_negative(victim))
		return -ENOENT;
	BUG_ON(!inode);

	BUG_ON(victim->d_parent->d_inode != dir);

	
	if (!uid_valid(i_uid_into_mnt(mnt_userns, inode)) ||
	    !gid_valid(i_gid_into_mnt(mnt_userns, inode)))
		return -EOVERFLOW;

	audit_inode_child(dir, victim, AUDIT_TYPE_CHILD_DELETE);

	error = inode_permission(mnt_userns, dir, MAY_WRITE | MAY_EXEC);
	if (error)
		return error;
	if (IS_APPEND(dir))
		return -EPERM;

	if (check_sticky(mnt_userns, dir, inode) || IS_APPEND(inode) ||
	    IS_IMMUTABLE(inode) || IS_SWAPFILE(inode) ||
	    HAS_UNMAPPED_ID(mnt_userns, inode))
		return -EPERM;
	if (isdir) {
		if (!d_is_dir(victim))
			return -ENOTDIR;
		if (IS_ROOT(victim))
			return -EBUSY;
	} else if (d_is_dir(victim))
		return -EISDIR;
	if (IS_DEADDIR(dir))
		return -ENOENT;
	if (victim->d_flags & DCACHE_NFSFS_RENAMED)
		return -EBUSY;
	return 0;
}

static inline int may_create(struct user_namespace *mnt_userns,
			     struct inode *dir, struct dentry *child)
{
	audit_inode_child(dir, child, AUDIT_TYPE_CHILD_CREATE);
	if (child->d_inode)
		return -EEXIST;
	if (IS_DEADDIR(dir))
		return -ENOENT;
	if (!fsuidgid_has_mapping(dir->i_sb, mnt_userns))
		return -EOVERFLOW;

	return inode_permission(mnt_userns, dir, MAY_WRITE | MAY_EXEC);
}

struct dentry *lock_rename(struct dentry *p1, struct dentry *p2)
{
	struct dentry *p;

	if (p1 == p2) {
		inode_lock_nested(p1->d_inode, I_MUTEX_PARENT);
		return NULL;
	}

	mutex_lock(&p1->d_sb->s_vfs_rename_mutex);

	p = d_ancestor(p2, p1);
	if (p) {
		inode_lock_nested(p2->d_inode, I_MUTEX_PARENT);
		inode_lock_nested(p1->d_inode, I_MUTEX_CHILD);
		return p;
	}

	p = d_ancestor(p1, p2);
	if (p) {
		inode_lock_nested(p1->d_inode, I_MUTEX_PARENT);
		inode_lock_nested(p2->d_inode, I_MUTEX_CHILD);
		return p;
	}

	inode_lock_nested(p1->d_inode, I_MUTEX_PARENT);
	inode_lock_nested(p2->d_inode, I_MUTEX_PARENT2);
	return NULL;
}

void unlock_rename(struct dentry *p1, struct dentry *p2)
{
	inode_unlock(p1->d_inode);
	if (p1 != p2) {
		inode_unlock(p2->d_inode);
		mutex_unlock(&p1->d_sb->s_vfs_rename_mutex);
	}
}

int vfs_create(struct user_namespace *mnt_userns, struct inode *dir,
	       struct dentry *dentry, umode_t mode, bool want_excl)
{
	int error = may_create(mnt_userns, dir, dentry);
	if (error)
		return error;

	if (!dir->i_op->create)
		return -EACCES;	
	mode &= S_IALLUGO;
	mode |= S_IFREG;
	error = security_inode_create(dir, dentry, mode);
	if (error)
		return error;
	error = dir->i_op->create(mnt_userns, dir, dentry, mode, want_excl);
	if (!error)
		fsnotify_create(dir, dentry);
	return error;
}

int vfs_mkobj(struct dentry *dentry, umode_t mode,
		int (*f)(struct dentry *, umode_t, void *),
		void *arg)
{
	struct inode *dir = dentry->d_parent->d_inode;
	int error = may_create(&init_user_ns, dir, dentry);
	if (error)
		return error;

	mode &= S_IALLUGO;
	mode |= S_IFREG;
	error = security_inode_create(dir, dentry, mode);
	if (error)
		return error;
	error = f(dentry, mode, arg);
	if (!error)
		fsnotify_create(dir, dentry);
	return error;
}

bool may_open_dev(const struct path *path)
{
	return !(path->mnt->mnt_flags & MNT_NODEV) &&
		!(path->mnt->mnt_sb->s_iflags & SB_I_NODEV);
}

static int may_open(struct user_namespace *mnt_userns, const struct path *path,
		    int acc_mode, int flag)
{
	struct dentry *dentry = path->dentry;
	struct inode *inode = dentry->d_inode;
	int error;

	if (!inode)
		return -ENOENT;

	switch (inode->i_mode & S_IFMT) {
	case S_IFLNK:
		return -ELOOP;
	case S_IFDIR:
		if (acc_mode & MAY_WRITE)
			return -EISDIR;
		if (acc_mode & MAY_EXEC)
			return -EACCES;
		break;
	case S_IFBLK:
	case S_IFCHR:
		if (!may_open_dev(path))
			return -EACCES;
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
		if  ((flag & O_ACCMODE) != O_RDONLY && !(flag & O_APPEND))
			return -EPERM;
		if (flag & O_TRUNC)
			return -EPERM;
	}

	
	if (flag & O_NOATIME && !inode_owner_or_capable(mnt_userns, inode))
		return -EPERM;

	return 0;
}

static int handle_truncate(struct user_namespace *mnt_userns, struct file *filp)
{
	const struct path *path = &filp->f_path;
	struct inode *inode = path->dentry->d_inode;
	int error = get_write_access(inode);
	if (error)
		return error;

	error = security_path_truncate(path);
	if (!error) {
		error = do_truncate(mnt_userns, path->dentry, 0,
				    ATTR_MTIME|ATTR_CTIME|ATTR_OPEN,
				    filp);
	}
	put_write_access(inode);
	return error;
}

static inline int open_to_namei_flags(int flag)
{
	if ((flag & O_ACCMODE) == 3)
		flag--;
	return flag;
}

static int may_o_create(struct user_namespace *mnt_userns,
			const struct path *dir, struct dentry *dentry,
			umode_t mode)
{
	int error = security_path_mknod(dir, dentry, mode, 0);
	if (error)
		return error;

	if (!fsuidgid_has_mapping(dir->dentry->d_sb, mnt_userns))
		return -EOVERFLOW;

	error = inode_permission(mnt_userns, dir->dentry->d_inode,
				 MAY_WRITE | MAY_EXEC);
	if (error)
		return error;

	return security_inode_create(dir->dentry->d_inode, dentry, mode);
}

static struct dentry *atomic_open(struct nameidata *nd, struct dentry *dentry,
				  struct file *file,
				  int open_flag, umode_t mode)
{
	struct dentry *const DENTRY_NOT_SET = (void *) -1UL;
	struct inode *dir =  nd->path.dentry->d_inode;
	int error;

	if (nd->flags & LOOKUP_DIRECTORY)
		open_flag |= O_DIRECTORY;

	file->f_path.dentry = DENTRY_NOT_SET;
	file->f_path.mnt = nd->path.mnt;
	error = dir->i_op->atomic_open(dir, dentry, file,
				       open_to_namei_flags(open_flag), mode);
	d_lookup_done(dentry);
	if (!error) {
		if (file->f_mode & FMODE_OPENED) {
			if (unlikely(dentry != file->f_path.dentry)) {
				dput(dentry);
				dentry = dget(file->f_path.dentry);
			}
		} else if (WARN_ON(file->f_path.dentry == DENTRY_NOT_SET)) {
			error = -EIO;
		} else {
			if (file->f_path.dentry) {
				dput(dentry);
				dentry = file->f_path.dentry;
			}
			if (unlikely(d_is_negative(dentry)))
				error = -ENOENT;
		}
	}
	if (error) {
		dput(dentry);
		dentry = ERR_PTR(error);
	}
	return dentry;
}

static struct dentry *lookup_open(struct nameidata *nd, struct file *file,
				  const struct open_flags *op,
				  bool got_write)
{
	struct user_namespace *mnt_userns;
	struct dentry *dir = nd->path.dentry;
	struct inode *dir_inode = dir->d_inode;
	int open_flag = op->open_flag;
	struct dentry *dentry;
	int error, create_error = 0;
	umode_t mode = op->mode;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);

	if (unlikely(IS_DEADDIR(dir_inode)))
		return ERR_PTR(-ENOENT);

	file->f_mode &= ~FMODE_CREATED;
	dentry = d_lookup(dir, &nd->last);
	for (;;) {
		if (!dentry) {
			dentry = d_alloc_parallel(dir, &nd->last, &wq);
			if (IS_ERR(dentry))
				return dentry;
		}
		if (d_in_lookup(dentry))
			break;

		error = d_revalidate(dentry, nd->flags);
		if (likely(error > 0))
			break;
		if (error)
			goto out_dput;
		d_invalidate(dentry);
		dput(dentry);
		dentry = NULL;
	}
	if (dentry->d_inode) {
		
		return dentry;
	}

	
	if (unlikely(!got_write))
		open_flag &= ~O_TRUNC;
	mnt_userns = mnt_user_ns(nd->path.mnt);
	if (open_flag & O_CREAT) {
		if (open_flag & O_EXCL)
			open_flag &= ~O_TRUNC;
		if (!IS_POSIXACL(dir->d_inode))
			mode &= ~current_umask();
		if (likely(got_write))
			create_error = may_o_create(mnt_userns, &nd->path,
						    dentry, mode);
		else
			create_error = -EROFS;
	}
	if (create_error)
		open_flag &= ~O_CREAT;
	if (dir_inode->i_op->atomic_open) {
		dentry = atomic_open(nd, dentry, file, open_flag, mode);
		if (unlikely(create_error) && dentry == ERR_PTR(-ENOENT))
			dentry = ERR_PTR(create_error);
		return dentry;
	}

	if (d_in_lookup(dentry)) {
		struct dentry *res = dir_inode->i_op->lookup(dir_inode, dentry,
							     nd->flags);
		d_lookup_done(dentry);
		if (unlikely(res)) {
			if (IS_ERR(res)) {
				error = PTR_ERR(res);
				goto out_dput;
			}
			dput(dentry);
			dentry = res;
		}
	}

	
	if (!dentry->d_inode && (open_flag & O_CREAT)) {
		file->f_mode |= FMODE_CREATED;
		audit_inode_child(dir_inode, dentry, AUDIT_TYPE_CHILD_CREATE);
		if (!dir_inode->i_op->create) {
			error = -EACCES;
			goto out_dput;
		}

		error = dir_inode->i_op->create(mnt_userns, dir_inode, dentry,
						mode, open_flag & O_EXCL);
		if (error)
			goto out_dput;
	}
	if (unlikely(create_error) && !dentry->d_inode) {
		error = create_error;
		goto out_dput;
	}
	return dentry;

out_dput:
	dput(dentry);
	return ERR_PTR(error);
}

static const char *open_last_lookups(struct nameidata *nd,
		   struct file *file, const struct open_flags *op)
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
		audit_inode(nd->name, dir, AUDIT_INODE_PARENT);
		
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
	dentry = lookup_open(nd, file, op, got_write);
	if (!IS_ERR(dentry) && (file->f_mode & FMODE_CREATED))
		fsnotify_create(dir->d_inode, dentry);
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
		nd->flags &= ~(LOOKUP_OPEN|LOOKUP_CREATE|LOOKUP_EXCL);
	return res;
}

static int do_open(struct nameidata *nd,
		   struct file *file, const struct open_flags *op)
{
	struct user_namespace *mnt_userns;
	int open_flag = op->open_flag;
	bool do_truncate;
	int acc_mode;
	int error;

	if (!(file->f_mode & (FMODE_OPENED | FMODE_CREATED))) {
		error = complete_walk(nd);
		if (error)
			return error;
	}
	if (!(file->f_mode & FMODE_CREATED))
		audit_inode(nd->name, nd->path.dentry, 0);
	mnt_userns = mnt_user_ns(nd->path.mnt);
	if (open_flag & O_CREAT) {
		if ((open_flag & O_EXCL) && !(file->f_mode & FMODE_CREATED))
			return -EEXIST;
		if (d_is_dir(nd->path.dentry))
			return -EISDIR;
		error = may_create_in_sticky(mnt_userns, nd,
					     d_backing_inode(nd->path.dentry));
		if (unlikely(error))
			return error;
	}
	if ((nd->flags & LOOKUP_DIRECTORY) && !d_can_lookup(nd->path.dentry))
		return -ENOTDIR;

	do_truncate = false;
	acc_mode = op->acc_mode;
	if (file->f_mode & FMODE_CREATED) {
		
		open_flag &= ~O_TRUNC;
		acc_mode = 0;
	} else if (d_is_reg(nd->path.dentry) && open_flag & O_TRUNC) {
		error = mnt_want_write(nd->path.mnt);
		if (error)
			return error;
		do_truncate = true;
	}
	error = may_open(mnt_userns, &nd->path, acc_mode, open_flag);
	if (!error && !(file->f_mode & FMODE_OPENED))
		error = vfs_open(&nd->path, file);
	if (!error)
		error = ima_file_check(file, op->acc_mode);
	if (!error && do_truncate)
		error = handle_truncate(mnt_userns, file);
	if (unlikely(error > 0)) {
		WARN_ON(1);
		error = -EINVAL;
	}
	if (do_truncate)
		mnt_drop_write(nd->path.mnt);
	return error;
}

struct dentry *vfs_tmpfile(struct user_namespace *mnt_userns,
			   struct dentry *dentry, umode_t mode, int open_flag)
{
	struct dentry *child = NULL;
	struct inode *dir = dentry->d_inode;
	struct inode *inode;
	int error;

	
	error = inode_permission(mnt_userns, dir, MAY_WRITE | MAY_EXEC);
	if (error)
		goto out_err;
	error = -EOPNOTSUPP;
	if (!dir->i_op->tmpfile)
		goto out_err;
	error = -ENOMEM;
	child = d_alloc(dentry, &slash_name);
	if (unlikely(!child))
		goto out_err;
	error = dir->i_op->tmpfile(mnt_userns, dir, child, mode);
	if (error)
		goto out_err;
	error = -ENOENT;
	inode = child->d_inode;
	if (unlikely(!inode))
		goto out_err;
	if (!(open_flag & O_EXCL)) {
		spin_lock(&inode->i_lock);
		inode->i_state |= I_LINKABLE;
		spin_unlock(&inode->i_lock);
	}
	ima_post_create_tmpfile(mnt_userns, inode);
	return child;

out_err:
	dput(child);
	return ERR_PTR(error);
}

static int do_tmpfile(struct nameidata *nd, unsigned flags,
		const struct open_flags *op,
		struct file *file)
{
	return -EOPNOTSUPP;
}

static int do_o_path(struct nameidata *nd, unsigned flags, struct file *file)
{
	struct path path;
	int error = path_lookupat(nd, flags, &path);
	if (!error) {
		audit_inode(nd->name, path.dentry, 0);
		error = vfs_open(&path, file);
		path_put(&path);
	}
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

	if (unlikely(file->f_flags & __O_TMPFILE)) {
		error = do_tmpfile(nd, flags, op, file);
	} else if (unlikely(file->f_flags & O_PATH)) {
		error = do_o_path(nd, flags, file);
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

struct file *do_file_open_root(const struct path *root,
		const char *name, const struct open_flags *op)
{
	struct nameidata nd;
	struct file *file;
	struct filename *filename;
	int flags = op->lookup_flags;

	if (d_is_symlink(root->dentry) && op->intent & LOOKUP_OPEN)
		return ERR_PTR(-ELOOP);

	filename = getname_kernel(name);
	if (IS_ERR(filename))
		return ERR_CAST(filename);

	set_nameidata(&nd, -1, filename, root);
	file = path_openat(&nd, op, flags | LOOKUP_RCU);
	if (unlikely(file == ERR_PTR(-ECHILD)))
		file = path_openat(&nd, op, flags);
	if (unlikely(file == ERR_PTR(-ESTALE)))
		file = path_openat(&nd, op, flags | LOOKUP_REVAL);
	restore_nameidata();
	putname(filename);
	return file;
}

static struct dentry *filename_create(int dfd, struct filename *name,
				      struct path *path, unsigned int lookup_flags)
{
	struct dentry *dentry = ERR_PTR(-EEXIST);
	struct qstr last;
	bool want_dir = lookup_flags & LOOKUP_DIRECTORY;
	unsigned int reval_flag = lookup_flags & LOOKUP_REVAL;
	unsigned int create_flags = LOOKUP_CREATE | LOOKUP_EXCL;
	int type;
	int err2;
	int error;

	error = filename_parentat(dfd, name, reval_flag, path, &last, &type);
	if (error)
		return ERR_PTR(error);

	
	if (unlikely(type != LAST_NORM))
		goto out;

	
	err2 = mnt_want_write(path->mnt);
	
	if (last.name[last.len] && !want_dir)
		create_flags = 0;
	inode_lock_nested(path->dentry->d_inode, I_MUTEX_PARENT);
	dentry = __lookup_hash(&last, path->dentry, reval_flag | create_flags);
	if (IS_ERR(dentry))
		goto unlock;

	error = -EEXIST;
	if (d_is_positive(dentry))
		goto fail;

	
	if (unlikely(!create_flags)) {
		error = -ENOENT;
		goto fail;
	}
	if (unlikely(err2)) {
		error = err2;
		goto fail;
	}
	return dentry;
fail:
	dput(dentry);
	dentry = ERR_PTR(error);
unlock:
	inode_unlock(path->dentry->d_inode);
	if (!err2)
		mnt_drop_write(path->mnt);
out:
	path_put(path);
	return dentry;
}

struct dentry *kern_path_create(int dfd, const char *pathname,
				struct path *path, unsigned int lookup_flags)
{
	struct filename *filename = getname_kernel(pathname);
	struct dentry *res = filename_create(dfd, filename, path, lookup_flags);

	putname(filename);
	return res;
}

void done_path_create(struct path *path, struct dentry *dentry)
{
	dput(dentry);
	inode_unlock(path->dentry->d_inode);
	mnt_drop_write(path->mnt);
	path_put(path);
}

inline struct dentry *user_path_create(int dfd, const char __user *pathname,
				struct path *path, unsigned int lookup_flags)
{
	struct filename *filename = getname(pathname);
	struct dentry *res = filename_create(dfd, filename, path, lookup_flags);

	putname(filename);
	return res;
}

int vfs_mknod(struct user_namespace *mnt_userns, struct inode *dir,
	      struct dentry *dentry, umode_t mode, dev_t dev)
{
	bool is_whiteout = S_ISCHR(mode) && dev == WHITEOUT_DEV;
	int error = may_create(mnt_userns, dir, dentry);

	if (error)
		return error;

	if ((S_ISCHR(mode) || S_ISBLK(mode)) && !is_whiteout &&
	    !capable(CAP_MKNOD))
		return -EPERM;

	if (!dir->i_op->mknod)
		return -EPERM;

	error = devcgroup_inode_mknod(mode, dev);
	if (error)
		return error;

	error = security_inode_mknod(dir, dentry, mode, dev);
	if (error)
		return error;

	error = dir->i_op->mknod(mnt_userns, dir, dentry, mode, dev);
	if (!error)
		fsnotify_create(dir, dentry);
	return error;
}

static int may_mknod(umode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFREG:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
	case 0: 
		return 0;
	case S_IFDIR:
		return -EPERM;
	default:
		return -EINVAL;
	}
}

static int do_mknodat(int dfd, struct filename *name, umode_t mode,
		unsigned int dev)
{
	putname(name);
	return -ENOSYS;
}

SYSCALL_DEFINE4(mknodat, int, dfd, const char __user *, filename, umode_t, mode,
		unsigned int, dev)
{
	return do_mknodat(dfd, getname(filename), mode, dev);
}

SYSCALL_DEFINE3(mknod, const char __user *, filename, umode_t, mode, unsigned, dev)
{
	return do_mknodat(AT_FDCWD, getname(filename), mode, dev);
}

int vfs_mkdir(struct user_namespace *mnt_userns, struct inode *dir,
	      struct dentry *dentry, umode_t mode)
{
	int error = may_create(mnt_userns, dir, dentry);
	unsigned max_links = dir->i_sb->s_max_links;

	if (error)
		return error;

	if (!dir->i_op->mkdir)
		return -EPERM;

	mode &= (S_IRWXUGO|S_ISVTX);
	error = security_inode_mkdir(dir, dentry, mode);
	if (error)
		return error;

	if (max_links && dir->i_nlink >= max_links)
		return -EMLINK;

	error = dir->i_op->mkdir(mnt_userns, dir, dentry, mode);
	if (!error)
		fsnotify_mkdir(dir, dentry);
	return error;
}

int do_mkdirat(int dfd, struct filename *name, umode_t mode)
{
	putname(name);
	return -ENOSYS;
}

SYSCALL_DEFINE3(mkdirat, int, dfd, const char __user *, pathname, umode_t, mode)
{
	return do_mkdirat(dfd, getname(pathname), mode);
}

SYSCALL_DEFINE2(mkdir, const char __user *, pathname, umode_t, mode)
{
	return do_mkdirat(AT_FDCWD, getname(pathname), mode);
}

int vfs_rmdir(struct user_namespace *mnt_userns, struct inode *dir,
		     struct dentry *dentry)
{
	/* Stub: directory removal not needed for minimal kernel */
	return -EPERM;
}

int do_rmdir(int dfd, struct filename *name)
{
	putname(name);
	return -ENOSYS;
}

SYSCALL_DEFINE1(rmdir, const char __user *, pathname)
{
	return do_rmdir(AT_FDCWD, getname(pathname));
}

int vfs_unlink(struct user_namespace *mnt_userns, struct inode *dir,
	       struct dentry *dentry, struct inode **delegated_inode)
{
	/* Stub: file unlinking not needed for minimal kernel */
	return -EPERM;
}

int do_unlinkat(int dfd, struct filename *name)
{
	putname(name);
	return -ENOSYS;
}

SYSCALL_DEFINE3(unlinkat, int, dfd, const char __user *, pathname, int, flag)
{
	if ((flag & ~AT_REMOVEDIR) != 0)
		return -EINVAL;

	if (flag & AT_REMOVEDIR)
		return do_rmdir(dfd, getname(pathname));
	return do_unlinkat(dfd, getname(pathname));
}

SYSCALL_DEFINE1(unlink, const char __user *, pathname)
{
	return do_unlinkat(AT_FDCWD, getname(pathname));
}

int vfs_symlink(struct user_namespace *mnt_userns, struct inode *dir,
		struct dentry *dentry, const char *oldname)
{
	int error = may_create(mnt_userns, dir, dentry);

	if (error)
		return error;

	if (!dir->i_op->symlink)
		return -EPERM;

	error = security_inode_symlink(dir, dentry, oldname);
	if (error)
		return error;

	error = dir->i_op->symlink(mnt_userns, dir, dentry, oldname);
	if (!error)
		fsnotify_create(dir, dentry);
	return error;
}

int do_symlinkat(struct filename *from, int newdfd, struct filename *to)
{
	putname(to);
	putname(from);
	return -ENOSYS;
}

SYSCALL_DEFINE3(symlinkat, const char __user *, oldname,
		int, newdfd, const char __user *, newname)
{
	return do_symlinkat(getname(oldname), newdfd, getname(newname));
}

SYSCALL_DEFINE2(symlink, const char __user *, oldname, const char __user *, newname)
{
	return do_symlinkat(getname(oldname), AT_FDCWD, getname(newname));
}

int vfs_link(struct dentry *old_dentry, struct user_namespace *mnt_userns,
	     struct inode *dir, struct dentry *new_dentry,
	     struct inode **delegated_inode)
{
	/* Stub: hard link creation not needed for minimal kernel */
	return -EPERM;
}

int do_linkat(int olddfd, struct filename *old, int newdfd,
	      struct filename *new, int flags)
{
	putname(old);
	putname(new);
	return -ENOSYS;
}

SYSCALL_DEFINE5(linkat, int, olddfd, const char __user *, oldname,
		int, newdfd, const char __user *, newname, int, flags)
{
	return do_linkat(olddfd, getname_uflags(oldname, flags),
		newdfd, getname(newname), flags);
}

SYSCALL_DEFINE2(link, const char __user *, oldname, const char __user *, newname)
{
	return do_linkat(AT_FDCWD, getname(oldname), AT_FDCWD, getname(newname), 0);
}

int vfs_rename(struct renamedata *rd)
{
	/* Stubbed: rename not needed for minimal kernel */
	return -EPERM;
}

int do_renameat2(int olddfd, struct filename *from, int newdfd,
		 struct filename *to, unsigned int flags)
{
	putname(from);
	putname(to);
	return -ENOSYS;
}

SYSCALL_DEFINE5(renameat2, int, olddfd, const char __user *, oldname,
		int, newdfd, const char __user *, newname, unsigned int, flags)
{
	return do_renameat2(olddfd, getname(oldname), newdfd, getname(newname),
				flags);
}

SYSCALL_DEFINE4(renameat, int, olddfd, const char __user *, oldname,
		int, newdfd, const char __user *, newname)
{
	return do_renameat2(olddfd, getname(oldname), newdfd, getname(newname),
				0);
}

SYSCALL_DEFINE2(rename, const char __user *, oldname, const char __user *, newname)
{
	return do_renameat2(AT_FDCWD, getname(oldname), AT_FDCWD,
				getname(newname), 0);
}

int readlink_copy(char __user *buffer, int buflen, const char *link)
{
	int len = PTR_ERR(link);
	if (IS_ERR(link))
		goto out;

	len = strlen(link);
	if (len > (unsigned) buflen)
		len = buflen;
	if (copy_to_user(buffer, link, len))
		len = -EFAULT;
out:
	return len;
}

int vfs_readlink(struct dentry *dentry, char __user *buffer, int buflen)
{
	struct inode *inode = d_inode(dentry);
	DEFINE_DELAYED_CALL(done);
	const char *link;
	int res;

	if (unlikely(!(inode->i_opflags & IOP_DEFAULT_READLINK))) {
		if (unlikely(inode->i_op->readlink))
			return inode->i_op->readlink(dentry, buffer, buflen);

		if (!d_is_symlink(dentry))
			return -EINVAL;

		spin_lock(&inode->i_lock);
		inode->i_opflags |= IOP_DEFAULT_READLINK;
		spin_unlock(&inode->i_lock);
	}

	link = READ_ONCE(inode->i_link);
	if (!link) {
		link = inode->i_op->get_link(dentry, inode, &done);
		if (IS_ERR(link))
			return PTR_ERR(link);
	}
	res = readlink_copy(buffer, buflen, link);
	do_delayed_call(&done);
	return res;
}

const char *vfs_get_link(struct dentry *dentry, struct delayed_call *done)
{
	const char *res = ERR_PTR(-EINVAL);
	struct inode *inode = d_inode(dentry);

	if (d_is_symlink(dentry)) {
		res = ERR_PTR(security_inode_readlink(dentry));
		if (!res)
			res = inode->i_op->get_link(dentry, inode, done);
	}
	return res;
}

const char *page_get_link(struct dentry *dentry, struct inode *inode,
			  struct delayed_call *callback)
{
	char *kaddr;
	struct page *page;
	struct address_space *mapping = inode->i_mapping;

	if (!dentry) {
		page = find_get_page(mapping, 0);
		if (!page)
			return ERR_PTR(-ECHILD);
		if (!PageUptodate(page)) {
			put_page(page);
			return ERR_PTR(-ECHILD);
		}
	} else {
		page = read_mapping_page(mapping, 0, NULL);
		if (IS_ERR(page))
			return (char*)page;
	}
	set_delayed_call(callback, page_put_link, page);
	BUG_ON(mapping_gfp_mask(mapping) & __GFP_HIGHMEM);
	kaddr = page_address(page);
	nd_terminate_link(kaddr, inode->i_size, PAGE_SIZE - 1);
	return kaddr;
}


void page_put_link(void *arg)
{
	put_page(arg);
}

int page_readlink(struct dentry *dentry, char __user *buffer, int buflen)
{
	DEFINE_DELAYED_CALL(done);
	int res = readlink_copy(buffer, buflen,
				page_get_link(dentry, d_inode(dentry),
					      &done));
	do_delayed_call(&done);
	return res;
}

int page_symlink(struct inode *inode, const char *symname, int len)
{
	/* Stub: symlink creation not needed for minimal kernel */
	return -EPERM;
}

const struct inode_operations page_symlink_inode_operations = {
	.get_link	= page_get_link,
};
