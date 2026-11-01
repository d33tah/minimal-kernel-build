#include <linux/namei.h>
#include <linux/file.h>
#include <linux/fs_struct.h>

#include "internal.h"

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

	__putname(name);
}

static int generic_permission(struct user_namespace *mnt_userns,
			      struct inode *inode, int mask)
{
	if (S_ISDIR(inode->i_mode))
		return 0;

	if (!(mask & MAY_EXEC) || (inode->i_mode & S_IXUGO))
		return 0;

	return -EACCES;
}

static inline int do_inode_permission(struct user_namespace *mnt_userns,
				      struct inode *inode, int mask)
{
	/* No live inode_operations sets ->permission; always generic. */
	return generic_permission(mnt_userns, inode, mask);
}

static int inode_permission(struct user_namespace *mnt_userns,
			    struct inode *inode, int mask)
{
	int retval;

	/* No superblock is ever read-only: there is no MAY_WRITE/-EROFS check. */
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
	dget(path->dentry);
}

void path_put(const struct path *path)
{
	dput(path->dentry);
}

struct nameidata {
	struct path path;
	struct qstr last;
	struct path root;
	struct inode *inode;
	unsigned int flags, state;
	int last_type;
	struct filename *name;
} __randomize_layout;

#define ND_ROOT_GRABBED 2

static void __set_nameidata(struct nameidata *p, struct filename *name)
{
	p->name = name;
	p->path.mnt = NULL;
	p->path.dentry = NULL;
}

static inline void set_nameidata(struct nameidata *p, struct filename *name)
{
	__set_nameidata(p, name);
	p->state = 0;
}

static void terminate_walk(struct nameidata *nd)
{
	/*
	 * No symlink is ever followed (see step_into), so there is no link
	 * stack: drop_links() and the nd->stack[] path_put loop are gone.
	 */
	path_put(&nd->path);
	if (nd->state & ND_ROOT_GRABBED) {
		path_put(&nd->root);
		nd->state &= ~ND_ROOT_GRABBED;
	}
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
}

static inline int traverse_mounts(struct path *path, bool *jumped)
{
	unsigned flags = smp_load_acquire(&path->dentry->d_flags);

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
	if (unlikely(ret)) {
		dput(path->dentry);
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
static struct dentry *__lookup_slow(const struct qstr *name, struct dentry *dir,
				    unsigned int flags)
{
	struct inode *inode = dir->d_inode;
	struct dentry *dentry = d_alloc(dir, name);
	struct dentry *old;

	if (unlikely(!dentry))
		return ERR_PTR(-ENOMEM);

	old = inode->i_op->lookup(inode, dentry, flags);
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
	return __lookup_slow(name, base, flags);
}

static struct dentry *lookup_slow(const struct qstr *name, struct dentry *dir,
				  unsigned int flags)
{
	struct inode *inode = dir->d_inode;
	struct dentry *res;
	inode_lock_shared(inode);
	res = __lookup_slow(name, dir, flags);
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
	dput(nd->path.dentry);
	nd->path = path;
	nd->inode = inode;
	return NULL;
}

static struct dentry *follow_dotdot(struct nameidata *nd)
{
	struct dentry *parent;

	if (path_equal(&nd->path, &nd->root))
		goto in_root;
	/* rare case of legitimate dget_parent()... */
	parent = dget_parent(nd->path.dentry);
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

		parent = follow_dotdot(nd);
		if (unlikely(!parent))
			error = step_into(nd, WALK_NOFOLLOW, nd->path.dentry);
		else
			error = step_into(nd, WALK_NOFOLLOW, parent);
		if (unlikely(error))
			return error;
	}
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
	int err;

	nd->last_type = LAST_ROOT;
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
				if (name[1] == '.')
					type = LAST_DOTDOT;
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
			return 0;
		}
		link = walk_component(nd, WALK_MORE);
		if (unlikely(link))
			return PTR_ERR(link);
		if (unlikely(!d_can_lookup(nd->path.dentry)))
			return -ENOTDIR;
	}
}

static const char *path_init(struct nameidata *nd, unsigned flags)
{
	const char *s = nd->name->name;

	nd->flags = flags;
	nd->root.mnt = NULL;

	if (*s == '/') {
		nd_jump_root(nd);
		return s;
	}

	get_fs_pwd(current->fs, &nd->path);
	nd->inode = nd->path.dentry->d_inode;
	return s;
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
	return retval;
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
	case S_IFCHR:
		if (acc_mode & MAY_EXEC)
			return -EACCES;
		flag &= ~O_TRUNC;
		break;
	}

	error = inode_permission(mnt_userns, inode, MAY_OPEN | acc_mode);
	if (error)
		return error;

	return 0;
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
	struct dentry *dentry;

	if (nd->last_type != LAST_NORM)
		return handle_dots(nd, nd->last_type);

	if (!(open_flag & O_CREAT)) {
		if (nd->last.name[nd->last.len])
			nd->flags |= LOOKUP_DIRECTORY;

		dentry = d_lookup(nd->path.dentry, &nd->last);
		if (likely(dentry))
			goto finish_lookup;
	} else {
		if (unlikely(nd->last.name[nd->last.len]))
			return ERR_PTR(-EISDIR);
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

	if (IS_ERR(dentry))
		return ERR_CAST(dentry);

	if (file->f_mode & (FMODE_OPENED | FMODE_CREATED)) {
		dput(nd->path.dentry);
		nd->path.dentry = dentry;
		return NULL;
	}

finish_lookup:
	return step_into(nd, WALK_TRAILING, dentry);
}

static int do_open(struct nameidata *nd, struct file *file,
		   const struct open_flags *op)
{
	struct user_namespace *mnt_userns;
	int open_flag = op->open_flag;
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

	/* No truncation support: nothing opens an existing file with O_TRUNC. */
	acc_mode = file->f_mode & FMODE_CREATED ? 0 : op->acc_mode;
	error = may_open(mnt_userns, &nd->path, acc_mode, open_flag);
	if (!error && !(file->f_mode & FMODE_OPENED))
		error = vfs_open(&nd->path, file);
	return error;
}

static struct file *path_openat(struct nameidata *nd,
				const struct open_flags *op, unsigned flags)
{
	struct file *file;
	const char *s;
	int error;

	file = alloc_empty_file(op->open_flag, current_cred());
	if (IS_ERR(file))
		return file;

	s = path_init(nd, flags);
	while (!(error = link_path_walk(s, nd)) &&
	       (s = open_last_lookups(nd, file, op)) != NULL)
		;
	if (!error)
		error = do_open(nd, file, op);
	terminate_walk(nd);
	if (likely(!error)) {
		if (likely(file->f_mode & FMODE_OPENED))
			return file;
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
	return filp;
}

static struct dentry *filename_create(struct filename *name, struct path *path,
				      unsigned int lookup_flags)
{
	struct dentry *dentry = ERR_PTR(-EEXIST);
	struct qstr last;
	bool want_dir = lookup_flags & LOOKUP_DIRECTORY;
	unsigned int create_flags = LOOKUP_CREATE | LOOKUP_EXCL;
	int type, error;

	error = filename_parentat(name, 0, path, &last, &type);
	if (error)
		return ERR_PTR(error);

	if (unlikely(type != LAST_NORM))
		goto out;

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
	return dentry;
fail:
	dput(dentry);
	dentry = ERR_PTR(error);
unlock:
	inode_unlock(path->dentry->d_inode);
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
