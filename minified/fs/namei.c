
#include <linux/kernel.h>
#include <linux/slab.h>
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
	result->uptr = NULL;
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

	mask &= 7;

	if (mask & (mode ^ (mode >> 3)))
		mode >>= 3;

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
	/* No live inode_operations sets ->permission; always generic. */
	return generic_permission(mnt_userns, inode, mask);
}

static int sb_permission(struct super_block *sb, struct inode *inode, int mask)
{
	if (unlikely(mask & MAY_WRITE)) {
		umode_t mode = inode->i_mode;

		if (sb_rdonly(sb) &&
		    (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
			return -EROFS;
	}
	return 0;
}

int inode_permission(struct user_namespace *mnt_userns, struct inode *inode,
		     int mask)
{
	int retval;

	retval = sb_permission(inode->i_sb, inode, mask);
	if (retval)
		return retval;

	if (unlikely(mask & MAY_WRITE)) {
		if (HAS_UNMAPPED_ID(mnt_userns, inode))
			return -EACCES;
	}

	retval = do_inode_permission(mnt_userns, inode, mask);
	if (retval)
		return retval;

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
	int last_type;
	unsigned depth;
	int total_link_count;
	struct saved {
		struct path link;
		const char *name;
	} *stack, internal[EMBEDDED_LEVELS];
	struct filename *name;
	struct nameidata *saved;
} __randomize_layout;

#define ND_ROOT_GRABBED 2
#define ND_JUMPED 4

static void __set_nameidata(struct nameidata *p, struct filename *name)
{
	struct nameidata *old = current->nameidata;
	p->stack = p->internal;
	p->depth = 0;
	p->name = name;
	p->path.mnt = NULL;
	p->path.dentry = NULL;
	p->total_link_count = old ? old->total_link_count : 0;
	p->saved = old;
	current->nameidata = p;
}

static inline void set_nameidata(struct nameidata *p, struct filename *name)
{
	__set_nameidata(p, name);
	p->state = 0;
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

static void terminate_walk(struct nameidata *nd)
{
	/*
	 * nd->depth is always 0 on this build (no symlink is ever followed --
	 * see step_into), so drop_links() and the nd->stack[] path_put loop
	 * were no-ops and have been removed.
	 */
	path_put(&nd->path);
	if (nd->state & ND_ROOT_GRABBED) {
		path_put(&nd->root);
		nd->state &= ~ND_ROOT_GRABBED;
	}
	nd->depth = 0;
	nd->path.mnt = NULL;
	nd->path.dentry = NULL;
}

static void nd_jump_root(struct nameidata *nd)
{
	if (!nd->root.mnt) {
		get_fs_root(current->fs, &nd->root);
		nd->state |= ND_ROOT_GRABBED;
	}
	path_put(&nd->path);
	nd->path = nd->root;
	path_get(&nd->path);
	nd->inode = nd->path.dentry->d_inode;
	nd->state |= ND_JUMPED;
}

static inline int traverse_mounts(struct path *path, bool *jumped)
{
	unsigned flags = smp_load_acquire(&path->dentry->d_flags);

	/*
	 * DCACHE_MANAGED_DENTRY (DCACHE_MOUNTED|DCACHE_NEED_AUTOMOUNT|
	 * DCACHE_MANAGE_TRANSIT) is never set on this build -- no mount is
	 * ever attached to a dentry -- so the managed-dentry traversal is
	 * unreachable and this always takes the fast path.
	 */
	*jumped = false;
	if (unlikely(d_flags_negative(flags)))
		return -ENOENT;
	return 0;
}

static inline int handle_mounts(struct nameidata *nd, struct dentry *dentry,
				struct path *path, struct inode **inode)
{
	bool jumped;
	int ret;

	path->mnt = nd->path.mnt;
	path->dentry = dentry;
	ret = traverse_mounts(path, &jumped);
	if (jumped) {
		nd->state |= ND_JUMPED;
	}
	if (unlikely(ret)) {
		dput(path->dentry);
		if (path->mnt != nd->path.mnt)
			mntput(path->mnt);
	} else {
		*inode = d_backing_inode(path->dentry);
	}
	return ret;
}

/*
 * No dentry_operations exist on this build, so a cached dentry is always
 * valid. A single task never races another lookup, so a dcache miss can
 * allocate and look up directly (no in-lookup hash / d_alloc_parallel).
 */
static struct dentry *lookup_real(const struct qstr *name, struct dentry *base,
				  unsigned int flags)
{
	struct inode *dir = base->d_inode;
	struct dentry *dentry = d_alloc(base, name);
	struct dentry *old;

	if (unlikely(!dentry))
		return ERR_PTR(-ENOMEM);

	old = dir->i_op->lookup(dir, dentry, flags);
	if (unlikely(old)) {
		dput(dentry);
		dentry = old;
	}
	return dentry;
}

static struct dentry *__lookup_hash(const struct qstr *name,
				    struct dentry *base, unsigned int flags)
{
	struct dentry *dentry = d_lookup(base, name);

	if (dentry)
		return dentry;
	return lookup_real(name, base, flags);
}

static struct dentry *lookup_slow(const struct qstr *name, struct dentry *dir,
				  unsigned int flags)
{
	struct inode *inode = dir->d_inode;
	struct dentry *res;
	inode_lock_shared(inode);
	res = lookup_real(name, dir, flags);
	inode_unlock_shared(inode);
	return res;
}

enum { WALK_TRAILING = 1, WALK_MORE = 2, WALK_NOFOLLOW = 4 };

static const char *step_into(struct nameidata *nd, int flags,
			     struct dentry *dentry)
{
	struct path path;
	struct inode *inode;
	int err = handle_mounts(nd, dentry, &path, &inode);

	if (err < 0)
		return ERR_PTR(err);
	/*
	 * No symlink inode can ever exist on this build (vfs_symlink is a stub
	 * returning -EPERM and no i_op->get_link is set anywhere), so
	 * d_is_symlink() is always false: the symlink-follow arm (pick_link)
	 * is unreachable and has been removed.
	 */
	dput(nd->path.dentry);
	if (nd->path.mnt != path.mnt)
		mntput(nd->path.mnt);
	nd->path = path;
	nd->inode = inode;
	return NULL;
}

static const char *handle_dots(struct nameidata *nd, int type)
{
	/*
	 * RUNTIME-DEAD ANCHOR-STUB: handle_dots only fires for non-LAST_NORM
	 * path components ("."/".."). This boot-once artifact only walks
	 * LAST_NORM paths, so it never runs. Returning NULL is the LAST_DOT
	 * (stay-put) no-op, behavior-preserving for the zero-dotdot boot.
	 * follow_dotdot/follow_dotdot_rcu/path_connected cascaded out.
	 */
	return NULL;
}

static const char *walk_component(struct nameidata *nd, int flags)
{
	struct dentry *dentry;

	if (unlikely(nd->last_type != LAST_NORM))
		return handle_dots(nd, nd->last_type);
	dentry = d_lookup(nd->path.dentry, &nd->last);
	if (unlikely(!dentry)) {
		dentry = lookup_slow(&nd->last, nd->path.dentry, nd->flags);
		if (IS_ERR(dentry))
			return ERR_CAST(dentry);
	}
	return step_into(nd, flags, dentry);
}

#include <asm/word-at-a-time.h>

#define HASH_MIX(x, y, a) \
	(x ^= (a), y ^= x, x = rol32(x, 7), x += y, y = rol32(y, 20), y *= 9)

static inline unsigned int fold_hash(unsigned long x, unsigned long y)
{
	return __hash_32(y ^ __hash_32(x));
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
		a = load_unaligned_zeropad(name + len);
		b = a ^ REPEAT_BYTE('/');
	} while (!(has_zero(a, &adata, &constants) |
		   has_zero(b, &bdata, &constants)));

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
	while (*name == '/')
		name++;
	if (!*name) {
		return 0;
	}

	for (;;) {
		struct user_namespace *mnt_userns;
		const char *link;
		u64 hash_len;
		int type;

		mnt_userns = mnt_user_ns(nd->path.mnt);
		err = inode_permission(mnt_userns, nd->inode, MAY_EXEC);
		if (err)
			return err;

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
			/*
			 * DCACHE_OP_HASH is never set on this build (no
			 * ->d_hash ops object exists), so the per-dentry
			 * name-hash override is unreachable.
			 */
			nd->state &= ~ND_JUMPED;
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
		if (unlikely(!d_can_lookup(nd->path.dentry)))
			return -ENOTDIR;
	}
}

static const char *path_init(struct nameidata *nd, unsigned flags)
{
	const char *s = nd->name->name;

	nd->flags = flags;
	nd->state |= ND_JUMPED;
	nd->root.mnt = NULL;

	if (*s == '/') {
		nd_jump_root(nd);
		return s;
	}

	get_fs_pwd(current->fs, &nd->path);
	nd->inode = nd->path.dentry->d_inode;
	return s;
}

static inline const char *lookup_last(struct nameidata *nd)
{
	if (nd->last_type == LAST_NORM && nd->last.name[nd->last.len])
		nd->flags |= LOOKUP_FOLLOW | LOOKUP_DIRECTORY;

	return walk_component(nd, WALK_TRAILING);
}

static int path_lookupat(struct nameidata *nd, unsigned flags,
			 struct path *path)
{
	const char *s = path_init(nd, flags);
	int err;

	while (!(err = link_path_walk(s, nd)) && (s = lookup_last(nd)) != NULL)
		;
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

int filename_lookup(struct filename *name, unsigned flags, struct path *path)
{
	int retval;
	struct nameidata nd;
	if (IS_ERR(name))
		return PTR_ERR(name);
	set_nameidata(&nd, name);
	retval = path_lookupat(&nd, flags, path);
	restore_nameidata();
	return retval;
}

static int path_parentat(struct nameidata *nd, unsigned flags,
			 struct path *parent)
{
	const char *s = path_init(nd, flags);
	int err = link_path_walk(s, nd);
	if (!err) {
		*parent = nd->path;
		nd->path.mnt = NULL;
		nd->path.dentry = NULL;
	}
	terminate_walk(nd);
	return err;
}

static int filename_parentat(struct filename *name, unsigned int flags,
			     struct path *parent, struct qstr *last, int *type)
{
	int retval;
	struct nameidata nd;

	if (IS_ERR(name))
		return PTR_ERR(name);
	set_nameidata(&nd, name);
	retval = path_parentat(&nd, flags, parent);
	if (likely(!retval)) {
		*last = nd.last;
		*type = nd.last_type;
	}
	restore_nameidata();
	return retval;
}

int kern_path(const char *name, unsigned int flags, struct path *path)
{
	struct filename *filename = getname_kernel(name);
	int ret = filename_lookup(filename, flags, path);

	putname(filename);
	return ret;
}

static inline int may_create(struct user_namespace *mnt_userns,
			     struct inode *dir, struct dentry *child)
{
	if (child->d_inode)
		return -EEXIST;
	if (!fsuidgid_has_mapping(dir->i_sb, mnt_userns))
		return -EOVERFLOW;

	return inode_permission(mnt_userns, dir, MAY_WRITE | MAY_EXEC);
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
	}

	error = inode_permission(mnt_userns, inode, MAY_OPEN | acc_mode);
	if (error)
		return error;

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

	error = do_truncate(mnt_userns, path->dentry, 0,
			    ATTR_MTIME | ATTR_CTIME, filp);
	put_write_access(inode);
	return error;
}

static struct dentry *lookup_open(struct nameidata *nd, struct file *file,
				  const struct open_flags *op)
{
	struct user_namespace *mnt_userns;
	struct dentry *dir = nd->path.dentry;
	struct inode *dir_inode = dir->d_inode;
	int open_flag = op->open_flag;
	struct dentry *dentry;
	int error;
	umode_t mode = op->mode;

	file->f_mode &= ~FMODE_CREATED;
	dentry = __lookup_hash(&nd->last, dir, nd->flags);
	if (IS_ERR(dentry) || dentry->d_inode)
		return dentry;

	mnt_userns = mnt_user_ns(nd->path.mnt);
	if ((open_flag & O_CREAT) && dir_inode->i_op->create) {
		mode &= ~current_umask();
		file->f_mode |= FMODE_CREATED;
		error = dir_inode->i_op->create(mnt_userns, dir_inode, dentry,
						mode, open_flag & O_EXCL);
		if (error) {
			dput(dentry);
			return ERR_PTR(error);
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
	struct dentry *dentry;
	const char *res;

	nd->flags |= op->intent;

	if (nd->last_type != LAST_NORM)
		return handle_dots(nd, nd->last_type);

	if (!(open_flag & O_CREAT)) {
		if (nd->last.name[nd->last.len])
			nd->flags |= LOOKUP_FOLLOW | LOOKUP_DIRECTORY;

		dentry = d_lookup(nd->path.dentry, &nd->last);
		if (likely(dentry))
			goto finish_lookup;
	} else {
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
	res = step_into(nd, WALK_TRAILING, dentry);
	if (unlikely(res))
		nd->flags &= ~(LOOKUP_OPEN | LOOKUP_CREATE | LOOKUP_EXCL);
	return res;
}

static int do_open(struct nameidata *nd, struct file *file,
		   const struct open_flags *op)
{
	struct user_namespace *mnt_userns;
	int open_flag = op->open_flag;
	bool do_truncate;
	int acc_mode, error;

	mnt_userns = mnt_user_ns(nd->path.mnt);
	if (open_flag & O_CREAT) {
		if ((open_flag & O_EXCL) && !(file->f_mode & FMODE_CREATED))
			return -EEXIST;
		if (d_is_dir(nd->path.dentry))
			return -EISDIR;
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

static struct file *path_openat(struct nameidata *nd,
				const struct open_flags *op, unsigned flags)
{
	struct file *file;
	int error;

	file = alloc_empty_file(op->open_flag, current_cred());
	if (IS_ERR(file))
		return file;

	if (unlikely(file->f_flags & O_PATH)) {
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
	return ERR_PTR(error);
}

struct file *do_filp_open(struct filename *pathname,
			  const struct open_flags *op)
{
	struct nameidata nd;
	int flags = op->lookup_flags;
	struct file *filp;

	set_nameidata(&nd, pathname);
	filp = path_openat(&nd, op, flags);
	restore_nameidata();
	return filp;
}

static struct dentry *filename_create(struct filename *name, struct path *path,
				      unsigned int lookup_flags)
{
	struct dentry *dentry = ERR_PTR(-EEXIST);
	struct qstr last;
	bool want_dir = lookup_flags & LOOKUP_DIRECTORY;
	unsigned int create_flags = LOOKUP_CREATE | LOOKUP_EXCL;
	int type, err2, error;

	error = filename_parentat(name, 0, path, &last, &type);
	if (error)
		return ERR_PTR(error);

	if (unlikely(type != LAST_NORM))
		goto out;

	err2 = mnt_want_write(path->mnt);

	if (last.name[last.len] && !want_dir)
		create_flags = 0;
	inode_lock_nested(path->dentry->d_inode, I_MUTEX_PARENT);
	dentry = __lookup_hash(&last, path->dentry, create_flags);
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

struct dentry *kern_path_create(const char *pathname, struct path *path,
				unsigned int lookup_flags)
{
	struct filename *filename = getname_kernel(pathname);
	struct dentry *res = filename_create(filename, path, lookup_flags);

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

int vfs_mknod(struct user_namespace *mnt_userns, struct inode *dir,
	      struct dentry *dentry, umode_t mode, dev_t dev)
{
	int error = may_create(mnt_userns, dir, dentry);

	if (error)
		return error;

	if (!dir->i_op->mknod)
		return -EPERM;

	error = dir->i_op->mknod(mnt_userns, dir, dentry, mode, dev);
	return error;
}

int vfs_mkdir(struct user_namespace *mnt_userns, struct inode *dir,
	      struct dentry *dentry, umode_t mode)
{
	int error = may_create(mnt_userns, dir, dentry);

	if (error)
		return error;

	if (!dir->i_op->mkdir)
		return -EPERM;

	mode &= (S_IRWXUGO | S_ISVTX);

	error = dir->i_op->mkdir(mnt_userns, dir, dentry, mode);
	return error;
}

int do_rmdir(struct filename *name)
{
	putname(name);
	return -ENOSYS;
}

int do_unlinkat(struct filename *name)
{
	putname(name);
	return -ENOSYS;
}

int vfs_symlink(struct user_namespace *mnt_userns, struct inode *dir,
		struct dentry *dentry, const char *oldname)
{
	/* Stub: symlink creation not needed for minimal kernel */
	return -EPERM;
}

int vfs_link(struct dentry *old_dentry, struct user_namespace *mnt_userns,
	     struct inode *dir, struct dentry *new_dentry,
	     struct inode **delegated_inode)
{
	/* Stub: hard link creation not needed for minimal kernel */
	return -EPERM;
}
