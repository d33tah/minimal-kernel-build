// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * Some corrections by tytso.
 */

/* [Feb 1997 T. Schoebel-Theuer] Complete rewrite of the pathname
 * lookup logic.
 */
/* [Feb-Apr 2000, AV] Rewrite to the new namespace architecture.
 */

#include <linux/namei.h>
#include <linux/file.h>
#include <linux/fs_struct.h>

#include "internal.h"

/* [Feb-1997 T. Schoebel-Theuer]
 * Fundamental changes in the pathname lookup mechanisms (namei)
 * were necessary because of omirr.  The reason is that omirr needs
 * to know the _real_ pathname, not the user-supplied one, in case
 * of symlinks (and also when transname replacements occur).
 *
 * The new code replaces the old recursive symlink resolution with
 * an iterative one (in case of non-nested symlink chains).  It does
 * this with calls to <fs>_follow_link().
 * As a side effect, dir_namei(), _namei() and follow_link() are now 
 * replaced with a single function lookup_dentry() that can handle all 
 * the special cases of the former code.
 *
 * With the new dcache, the pathname is stored at each inode, at least as
 * long as the refcount of the inode is positive.  As a side effect, the
 * size of the dcache depends on the inode cache and thus is dynamic.
 *
 * [29-Apr-1998 C. Scott Ananian] Updated above description of symlink
 * resolution to correspond with current state of the code.
 *
 * Note that the symlink resolution is not *completely* iterative.
 * There is still a significant amount of tail- and mid- recursion in
 * the algorithm.  Also, note that <fs>_readlink() is not used in
 * lookup_dentry(): lookup_dentry() on the result of <fs>_readlink()
 * may return different results than <fs>_follow_link().  Many virtual
 * filesystems (including /proc) exhibit this behavior.
 */

/* [24-Feb-97 T. Schoebel-Theuer] Side effects caused by new implementation:
 * New symlink semantics: when open() is called with flags O_CREAT | O_EXCL
 * and the name already exists in form of a symlink, try to create the new
 * name indicated by the symlink. The old code always complained that the
 * name already exists, due to not following the symlink even if its target
 * is nonexistent.  The new semantics affects also mknod() and link() when
 * the name is a symlink pointing to a non-existent name.
 *
 * I don't know which semantics is the right one, since I have no access
 * to standards. But I found by trial that HP-UX 9.0 has the full "new"
 * semantics implemented, while SunOS 4.1.1 and Solaris (SunOS 5.4) have the
 * "old" one. Personally, I think the new semantics is much more logical.
 * Note that "ln old new" where "new" is a symlink pointing to a non-existing
 * file does succeed in both HP-UX and SunOs, but not in Solaris
 * and in the old Linux semantics.
 */

/* [16-Dec-97 Kevin Buhr] For security reasons, we change some symlink
 * semantics.  See the comments in "open_namei" and "do_link" below.
 *
 * [10-Sep-98 Alan Modra] Another symlink change.
 */

/* [Feb-Apr 2000 AV] Complete rewrite. Rules for symlinks:
 *	inside the path - always follow.
 *	in the last component in creation/removal/renaming - never follow.
 *	if LOOKUP_FOLLOW passed - follow.
 *	if the pathname has trailing slashes - follow.
 *	otherwise - don't follow.
 * (applied in that order).
 *
 * [Jun 2000 AV] Inconsistent behaviour of open() in case if flags==O_CREAT
 * restored for 2.4. This is the last surviving part of old 4.2BSD bug.
 * During the 2.4 we need to fix the userland stuff depending on it -
 * hopefully we will be able to get rid of that wart in 2.5. So far only
 * XEmacs seems to be relying on it...
 */
/*
 * [Sep 2001 AV] Single-semaphore locking scheme (kudos to David Holland)
 * implemented.  Let's see if raised priority of ->s_vfs_rename_mutex gives
 * any extra contention...
 */

/* In order to reduce some races, while at the same time doing additional
 * checking and hopefully speeding things up, we copy filenames to the
 * kernel data space before using them..
 *
 * POSIX.1 2.4: an empty pathname is invalid (ENOENT).
 * PATH_MAX includes the nul terminator --RR.
 */

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

/**
 * do_inode_permission - UNIX permission checking
 * @mnt_userns:	user namespace of the mount the inode was found from
 * @inode:	inode to check permissions on
 * @mask:	right to check for (%MAY_READ, %MAY_WRITE, %MAY_EXEC ...)
 *
 * We _really_ want to just do "generic_permission()" without
 * even looking at the inode->i_op values. So we keep a cache
 * flag in inode->i_opflags, that says "this has not special
 * permission function, use the fast case".
 */
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
		/*
		 * Updating mtime will likely cause i_uid and i_gid to be
		 * written back improperly if their true value is unknown
		 * to the vfs.
		 */
		if (HAS_UNMAPPED_ID(mnt_userns, inode))
			return -EACCES;
	}

	retval = do_inode_permission(mnt_userns, inode, mask);
	if (retval)
		return retval;

	return 0;
}

/**
 * path_get - get a reference to a path
 * @path: path to get the reference to
 *
 * Given a path increment the reference count to the dentry and the vfsmount.
 */
void path_get(const struct path *path)
{
	dget(path->dentry);
}

/**
 * path_put - put a reference to a path
 * @path: path to put the reference to
 *
 * Given a path decrement the reference count to the dentry and the vfsmount.
 */
void path_put(const struct path *path)
{
	dput(path->dentry);
}

struct nameidata {
	struct path path;
	struct qstr last;
	struct path root;
	struct inode *inode; /* path.dentry.d_inode */
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

/*
 * Parent directory has inode locked exclusive.  This is one
 * and only case when ->lookup() gets called on non in-lookup
 * dentries - as the matter of fact, this only gets called
 * when directory is guaranteed to have no in-lookup children
 * at all.
 */
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
	/* Use arch-optimized multiply if one exists */
	return __hash_32(y ^ __hash_32(x));
}

/*
 * Calculate the length and hash of the path component, and
 * return the "hash_len" as the result.
 */
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

/*
 * Name resolution.
 * This is the basic name resolution function, turning a pathname into
 * the final dentry. We expect 'base' to be positive and a directory.
 *
 * Returns 0 and nd will have valid dentry and mnt on success.
 * Returns error and drops reference to input namei data on failure.
 */
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

	/* At this point we know we have a real path component. */
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

		/*
		 * If it wasn't NUL, we know it was '/'. Skip that
		 * slash, and continue until no more slashes.
		 */
		do {
			name++;
		} while (unlikely(*name == '/'));
		if (unlikely(!*name)) {
OK:
			return 0;
		}
		/* not the last component */
		link = walk_component(nd, WALK_MORE);
		if (unlikely(link))
			return PTR_ERR(link);
		if (unlikely(!d_can_lookup(nd->path.dentry)))
			return -ENOTDIR;
	}
}

/* must be paired with terminate_walk() */
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

/* Returns 0 and nd will be valid on success; Retuns error, otherwise. */
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

/*	Check whether we can create an object with dentry child in directory
 *  dir.
 *  1. We can't do it if child already exists (open has special treatment for
 *     this case, but since we are inlined it's OK)
 *  2. We can't do it if dir is read-only (done in permission())
 *  3. We can't do it if the fs can't represent the fsuid or fsgid.
 *  4. We should have write and exec permissions on dir
 *  5. We can't do it if dir is immutable (done in permission())
 */
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

/*
 * Look up and maybe create and open the last component.
 *
 * Must be called with parent locked (exclusive in O_CREAT case).
 *
 * Returns 0 on success, that is, if
 *  the file was successfully atomically created (if necessary) and opened, or
 *  the file was not completely opened at this time, though lookups and
 *  creations were performed.
 * These case are distinguished by presence of FMODE_OPENED on file->f_mode.
 * In the latter case dentry returned in @path might be negative if O_CREAT
 * hadn't been specified.
 *
 * An error code is returned on failure.
 */
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
		/* trailing slashes? */
		if (unlikely(nd->last.name[nd->last.len]))
			return ERR_PTR(-EISDIR);
		/*
		 * do _not_ fail yet - we might not need that or fail with
		 * a different error; let lookup_open() decide; we'll be
		 * dropping this one anyway.
		 */
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

	/*
	 * Yucky last component or no last component at all?
	 * (foo/., foo/.., /////)
	 */
	if (unlikely(type != LAST_NORM))
		goto out;

	/*
	 * Do the final lookup.  Suppress 'create' if there is a trailing
	 * '/', and a directory wasn't requested.
	 */
	if (last.name[last.len] && !want_dir)
		create_flags = 0;
	inode_lock_nested(path->dentry->d_inode, I_MUTEX_PARENT);
	dentry = __lookup_hash(&last, path->dentry, create_flags);
	if (IS_ERR(dentry))
		goto unlock;

	error = -EEXIST;
	if (d_is_positive(dentry))
		goto fail;

	/*
	 * Special case - lookup gave negative, but... we had foo/bar/
	 * From the vfs_mknod() POV we just have a negative dentry -
	 * all is fine. Let's be bastards - you had / on the end, you've
	 * been asking for (non-existent) directory. -ENOENT for you.
	 */
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

/**
 * vfs_mknod - create device node or file
 * @mnt_userns:	user namespace of the mount the inode was found from
 * @dir:	inode of @dentry
 * @dentry:	pointer to dentry of the base directory
 * @mode:	mode of the new device node or file
 * @dev:	device number of device to create
 *
 * Create a device node or file.
 *
 * If the inode has been found through an idmapped mount the user namespace of
 * the vfsmount must be passed through @mnt_userns. This function will then take
 * care to map the inode according to @mnt_userns before checking permissions.
 * On non-idmapped mounts or if permission checking is to be performed on the
 * raw inode simply passs init_user_ns.
 */
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

/**
 * vfs_mkdir - create directory
 * @mnt_userns:	user namespace of the mount the inode was found from
 * @dir:	inode of @dentry
 * @dentry:	pointer to dentry of the base directory
 * @mode:	mode of the new directory
 *
 * Create a directory.
 *
 * If the inode has been found through an idmapped mount the user namespace of
 * the vfsmount must be passed through @mnt_userns. This function will then take
 * care to map the inode according to @mnt_userns before checking permissions.
 * On non-idmapped mounts or if permission checking is to be performed on the
 * raw inode simply passs init_user_ns.
 */
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
