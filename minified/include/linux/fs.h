/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FS_H
#define _LINUX_FS_H

#include <linux/linkage.h>
#include <linux/kdev_t.h>
#include <linux/dcache.h>
#include <linux/path.h>
#include <linux/gfp.h>
#include <linux/xarray.h>
#include <linux/semaphore.h>
#include <linux/stat.h>
#include <asm/fcntl.h>

#include <linux/mount.h>
#include <linux/cred.h>
#include <linux/mnt_idmapping.h>
#include <linux/slab.h>

#define INR_OPEN_CUR 1024
#define INR_OPEN_MAX 4096
#define SEEK_END 2

struct vm_area_struct;
struct iov_iter;
struct fs_context;

#define MAY_EXEC 0x00000001
#define MAY_WRITE 0x00000002
#define MAY_READ 0x00000004
#define MAY_OPEN 0x00000020

/*
 * flags in file.f_mode.  Note that FMODE_READ and FMODE_WRITE must correspond
 * to O_WRONLY and O_RDWR via the strange trick in do_dentry_open()
 */

/* file is open for reading */
#define FMODE_READ ((__force fmode_t)0x1)

/* file is open for writing */
#define FMODE_WRITE ((__force fmode_t)0x2)

/* File is opened for execution with sys_execve / sys_uselib */
#define FMODE_EXEC ((__force fmode_t)0x20)

/* Write access to underlying fs */
#define FMODE_WRITER ((__force fmode_t)0x10000)

/* Has read method(s) */
#define FMODE_CAN_READ ((__force fmode_t)0x20000)

/* Has write method(s) */
#define FMODE_CAN_WRITE ((__force fmode_t)0x40000)

#define FMODE_OPENED ((__force fmode_t)0x80000)
#define FMODE_CREATED ((__force fmode_t)0x100000)

/* File was opened by fanotify and shouldn't generate fanotify events */
#define FMODE_NONOTIFY ((__force fmode_t)0x4000000)

/* Reduced positive_aop_returns - only AOP_TRUNCATED_PAGE used */
enum positive_aop_returns { AOP_TRUNCATED_PAGE = 0x80001 };

/*
 * oh the beauties of C type declarations.
 */
struct page;
struct address_space;

struct kiocb {
	struct file *ki_filp;
	randomized_struct_fields_start loff_t ki_pos;
	randomized_struct_fields_end
};

struct address_space_operations {
	int (*read_folio)(struct file *, struct folio *);

	int (*write_begin)(struct file *, struct address_space *mapping,
			   loff_t pos, unsigned len, struct page **pagep,
			   void **fsdata);
	int (*write_end)(struct file *, struct address_space *mapping,
			 loff_t pos, unsigned len, unsigned copied,
			 struct page *page, void *fsdata);
};

/**
 * struct address_space - Contents of a cacheable, mappable object.
 * @host: Owner, either the inode or the block_device.
 * @i_pages: Cached pages.
 * @invalidate_lock: Guards coherency between page cache contents and
 *   file offset->disk block mappings in the filesystem during invalidates.
 *   It is also used to block modification of page cache contents through
 *   memory mappings.
 * @gfp_mask: Memory allocation flags to use for allocating pages.
 * @i_mmap_writable: Number of VM_SHARED mappings.
 * @nr_thps: Number of THPs in the pagecache (non-shmem only).
 * @i_mmap: Tree of private and shared mappings.
 * @i_mmap_rwsem: Protects @i_mmap and @i_mmap_writable.
 * @nrpages: Number of page entries, protected by the i_pages lock.
 * @writeback_index: Writeback starts here.
 * @a_ops: Methods.
 * @flags: Error bits and flags (AS_*).
 * @wb_err: The most recent error which has occurred.
 * @private_lock: For use by the owner of the address_space.
 * @private_list: For use by the owner of the address_space.
 * @private_data: For use by the owner of the address_space.
 */
struct address_space {
	struct inode *host;
	struct xarray i_pages;
	struct rw_semaphore invalidate_lock;
	gfp_t gfp_mask;
	const struct address_space_operations *a_ops;
} __attribute__((aligned(sizeof(long)))) __randomize_layout;

/*
 * Keep mostly read-only and often accessed (especially for
 * the RCU path lookup and 'stat' data) fields at the beginning
 * of the 'struct inode'
 */
struct inode {
	umode_t i_mode;
	kuid_t i_uid;
	kgid_t i_gid;
	const struct inode_operations *i_op;
	struct super_block *i_sb;
	struct address_space *i_mapping;
	dev_t i_rdev;
	loff_t i_size;
	spinlock_t i_lock; /* i_blocks, i_bytes, maybe i_size */
	struct rw_semaphore i_rwsem;
	atomic_t i_writecount;
	const struct file_operations *i_fop; /* former ->i_op->default_file_ops */
	struct address_space i_data;
	struct cdev *i_cdev;
} __randomize_layout;

enum inode_i_mutex_lock_class { I_MUTEX_PARENT };

static inline void inode_lock(struct inode *inode)
{
	down_write(&inode->i_rwsem);
}

static inline void inode_unlock(struct inode *inode)
{
	up_write(&inode->i_rwsem);
}

static inline void inode_lock_shared(struct inode *inode)
{
	down_read(&inode->i_rwsem);
}

static inline void inode_unlock_shared(struct inode *inode)
{
	up_read(&inode->i_rwsem);
}

static inline void inode_lock_nested(struct inode *inode, unsigned subclass)
{
	down_write_nested(&inode->i_rwsem, subclass);
}

static inline void filemap_invalidate_lock_shared(struct address_space *mapping)
{
	down_read(&mapping->invalidate_lock);
}

static inline void
filemap_invalidate_unlock_shared(struct address_space *mapping)
{
	up_read(&mapping->invalidate_lock);
}

/*
 * NOTE: in a 32bit arch with a preemptable kernel and
 * an UP compile the i_size_read/write must be atomic
 * with respect to the local cpu (unlike with preempt disabled),
 * but they don't need to be atomic with respect to other cpus like in
 * true SMP (so they need either to either locally disable irq around
 * the read or for example on x86 they can be still implemented as a
 * cmpxchg8b without the need of the lock prefix). For SMP compiles
 * and 64bit archs it makes no difference if preempt is enabled or not.
 */
static inline loff_t i_size_read(const struct inode *inode)
{
	return inode->i_size;
}

/*
 * NOTE: unlike i_size_read(), i_size_write() does need locking around it
 * (normally i_mutex), otherwise on 32bit/SMP an update of i_size_seqcount
 * can be lost, resulting in subsequent i_size_read() calls spinning forever.
 */
static inline void i_size_write(struct inode *inode, loff_t i_size)
{
	inode->i_size = i_size;
}

struct file {
	union {
		struct rcu_head fu_rcuhead;
	} f_u;
	struct path f_path;
	struct inode *f_inode; /* cached value */
	const struct file_operations *f_op;
	atomic_long_t f_count;
	fmode_t f_mode;
	loff_t f_pos;
	void *private_data;
	struct address_space *f_mapping;
} __randomize_layout __attribute__((aligned(4)));

static inline struct file *get_file(struct file *f)
{
	atomic_long_inc(&f->f_count);
	return f;
}

#define MAX_NON_LFS ((1UL << 31) - 1)

/* 32-bit only kernel */
#define MAX_LFS_FILESIZE ((loff_t)ULONG_MAX << PAGE_SHIFT)

static inline struct inode *file_inode(const struct file *f)
{
	return f->f_inode;
}

struct super_block {
	loff_t s_maxbytes; /* Max file size */
	struct dentry *s_root;
	struct hlist_bl_head s_roots; /* alternate root dentries for NFS */

	/*
	 * Owning user namespace and default context in which to
	 * interpret filesystem uids, gids, quotas, device nodes,
	 * xattrs and security labels.
	 */
	struct user_namespace *s_user_ns;
} __randomize_layout;

static inline struct user_namespace *i_user_ns(const struct inode *inode)
{
	return inode->i_sb->s_user_ns;
}

static inline void i_uid_write(struct inode *inode, uid_t uid)
{
	inode->i_uid = make_kuid(i_user_ns(inode), uid);
}

static inline void i_gid_write(struct inode *inode, gid_t gid)
{
	inode->i_gid = make_kgid(i_user_ns(inode), gid);
}

/**
 * i_uid_into_mnt - map an inode's i_uid down into a mnt_userns
 * @mnt_userns: user namespace of the mount the inode was found from
 * @inode: inode to map
 *
 * Return: the inode's i_uid mapped down according to @mnt_userns.
 * If the inode's i_uid has no mapping INVALID_UID is returned.
 */
static inline kuid_t i_uid_into_mnt(struct user_namespace *mnt_userns,
				    const struct inode *inode)
{
	return mapped_kuid_fs(mnt_userns, i_user_ns(inode), inode->i_uid);
}

/**
 * i_gid_into_mnt - map an inode's i_gid down into a mnt_userns
 * @mnt_userns: user namespace of the mount the inode was found from
 * @inode: inode to map
 *
 * Return: the inode's i_gid mapped down according to @mnt_userns.
 * If the inode's i_gid has no mapping INVALID_GID is returned.
 */
static inline kgid_t i_gid_into_mnt(struct user_namespace *mnt_userns,
				    const struct inode *inode)
{
	return mapped_kgid_fs(mnt_userns, i_user_ns(inode), inode->i_gid);
}

/**
 * inode_fsuid_set - initialize inode's i_uid field with callers fsuid
 * @inode: inode to initialize
 * @mnt_userns: user namespace of the mount the inode was found from
 *
 * Initialize the i_uid field of @inode. If the inode was found/created via
 * an idmapped mount map the caller's fsuid according to @mnt_users.
 */
static inline void inode_fsuid_set(struct inode *inode,
				   struct user_namespace *mnt_userns)
{
	inode->i_uid = mapped_fsuid(mnt_userns, i_user_ns(inode));
}

/**
 * inode_fsgid_set - initialize inode's i_gid field with callers fsgid
 * @inode: inode to initialize
 * @mnt_userns: user namespace of the mount the inode was found from
 *
 * Initialize the i_gid field of @inode. If the inode was found/created via
 * an idmapped mount map the caller's fsgid according to @mnt_users.
 */
static inline void inode_fsgid_set(struct inode *inode,
				   struct user_namespace *mnt_userns)
{
	inode->i_gid = mapped_fsgid(mnt_userns, i_user_ns(inode));
}

/**
 * fsuidgid_has_mapping() - check whether caller's fsuid/fsgid is mapped
 * @sb: the superblock we want a mapping in
 * @mnt_userns: user namespace of the relevant mount
 *
 * Check whether the caller's fsuid and fsgid have a valid mapping in the
 * s_user_ns of the superblock @sb. If the caller is on an idmapped mount map
 * the caller's fsuid and fsgid according to the @mnt_userns first.
 *
 * Return: true if fsuid and fsgid is mapped, false if not.
 */
static inline bool fsuidgid_has_mapping(struct super_block *sb,
					struct user_namespace *mnt_userns)
{
	struct user_namespace *fs_userns = sb->s_user_ns;
	kuid_t kuid;
	kgid_t kgid;

	kuid = mapped_fsuid(mnt_userns, fs_userns);
	if (!uid_valid(kuid))
		return false;
	kgid = mapped_fsgid(mnt_userns, fs_userns);
	if (!gid_valid(kgid))
		return false;
	return kuid_has_mapping(fs_userns, kuid) &&
	       kgid_has_mapping(fs_userns, kgid);
}

int vfs_mkdir(struct user_namespace *, struct inode *, struct dentry *,
	      umode_t);
int vfs_mknod(struct user_namespace *, struct inode *, struct dentry *, umode_t,
	      dev_t);

/*
 * VFS file helper functions.
 */
void inode_init_owner(struct user_namespace *mnt_userns, struct inode *inode,
		      const struct inode *dir, umode_t mode);

struct file_operations {
	ssize_t (*read_iter)(struct kiocb *, struct iov_iter *);
	ssize_t (*write_iter)(struct kiocb *, struct iov_iter *);
	int (*mmap)(struct file *, struct vm_area_struct *);
	int (*open)(struct inode *, struct file *);
} __randomize_layout;

struct inode_operations {
	struct dentry *(*lookup)(struct inode *, struct dentry *, unsigned int);
	int (*create)(struct user_namespace *, struct inode *, struct dentry *,
		      umode_t, bool);
	int (*mkdir)(struct user_namespace *, struct inode *, struct dentry *,
		     umode_t);
	int (*mknod)(struct user_namespace *, struct inode *, struct dentry *,
		     umode_t, dev_t);
} ____cacheline_aligned;

static inline ssize_t call_write_iter(struct file *file, struct kiocb *kio,
				      struct iov_iter *iter)
{
	return file->f_op->write_iter(kio, iter);
}

static inline int call_mmap(struct file *file, struct vm_area_struct *vma)
{
	return file->f_op->mmap(file, vma);
}

static inline bool HAS_UNMAPPED_ID(struct user_namespace *mnt_userns,
				   struct inode *inode)
{
	return !uid_valid(i_uid_into_mnt(mnt_userns, inode)) ||
	       !gid_valid(i_gid_into_mnt(mnt_userns, inode));
}

static inline void init_sync_kiocb(struct kiocb *kiocb, struct file *filp)
{
	*kiocb = (struct kiocb){
		.ki_filp = filp,
	};
}

struct file_system_type {
	int (*init_fs_context)(struct fs_context *);
};

#define fops_get(fops) ((fops) ? (fops) : NULL)
/*
 * This one is to be used *ONLY* from ->open() instances.
 * fops must be non-NULL, pinned down *and* module dependencies
 * should be sufficient to pin the caller down as well.
 */
#define replace_fops(f, fops)                     \
	do {                                      \
		struct file *__file = (f);        \
		BUG_ON(!(__file->f_op = (fops))); \
	} while (0)

extern int current_umask(void);

#define MAX_RW_COUNT (INT_MAX & PAGE_MASK)

struct filename {
	const char *name; /* pointer to actual string */
	int refcnt;
	const char iname[];
};
static_assert(offsetof(struct filename, iname) % sizeof(long) == 0);

extern struct file *filp_open(const char *, int, umode_t);

extern struct filename *getname_kernel(const char *);
extern void putname(struct filename *name);

/* fs/dcache.c */
extern void __init vfs_caches_init_early(void);
extern void __init vfs_caches_init(void);

#define __getname() kmalloc(PATH_MAX, GFP_KERNEL)
#define __putname(name) kfree(name)

extern const struct file_operations def_chr_fops;

extern void init_special_inode(struct inode *, umode_t, dev_t);

/*
 * Sync the bytes written if this was a synchronous write.  Expect ki_pos
 * to already be updated for the write, and will return either the amount
 * of bytes passed in, or an error if syncing the file failed.
 */
static inline ssize_t generic_write_sync(struct kiocb *iocb, ssize_t count)
{
	return count;
}

/*
 * This is used for regular files where some users -- especially the
 * currently executed binary in a process, previously handled via
 * VM_DENYWRITE -- cannot handle concurrent write (and maybe mmap
 * read-write shared) accesses.
 *
 * get_write_access() gets write permission for a file.
 * put_write_access() releases this write permission.
 * deny_write_access() denies write access to a file.
 * allow_write_access() re-enables write access to a file.
 *
 * The i_writecount field of an inode can have the following values:
 * 0: no write access, no denied write access
 * < 0: (-i_writecount) users that denied write access to the file.
 * > 0: (i_writecount) users that have write access to the file.
 *
 * Normally we operate on that counter with atomic_{inc,dec} and it's safe
 * except for the cases where we don't hold i_writecount yet. Then we need to
 * use {get,deny}_write_access() - these functions check the sign and refuse
 * to do the change if sign is wrong.
 */
static inline int get_write_access(struct inode *inode)
{
	return atomic_inc_unless_negative(&inode->i_writecount) ? 0 : -ETXTBSY;
}
static inline int deny_write_access(struct file *file)
{
	struct inode *inode = file_inode(file);
	return atomic_dec_unless_positive(&inode->i_writecount) ? 0 : -ETXTBSY;
}
static inline void put_write_access(struct inode *inode)
{
	atomic_dec(&inode->i_writecount);
}
static inline void allow_write_access(struct file *file)
{
	if (file)
		atomic_inc(&file_inode(file)->i_writecount);
}
extern ssize_t kernel_read(struct file *, void *, size_t, loff_t *);
extern ssize_t kernel_write(struct file *, const void *, size_t, loff_t *);
extern struct inode *new_inode(struct super_block *sb);

extern int generic_file_mmap(struct file *, struct vm_area_struct *);
extern ssize_t generic_write_checks(struct kiocb *, struct iov_iter *);
extern ssize_t generic_file_read_iter(struct kiocb *, struct iov_iter *);
extern ssize_t generic_file_write_iter(struct kiocb *, struct iov_iter *);

#define special_file(m) (S_ISCHR(m) || S_ISBLK(m) || S_ISFIFO(m) || S_ISSOCK(m))

extern const struct address_space_operations ram_aops;

extern struct dentry *simple_lookup(struct inode *, struct dentry *,
				    unsigned int flags);

#define __FMODE_EXEC ((__force int)FMODE_EXEC)
#define __FMODE_NONOTIFY ((__force int)FMODE_NONOTIFY)

#define ACC_MODE(x) ("\004\002\006\006"[(x) & O_ACCMODE])
#define OPEN_FMODE(flag)                              \
	((__force fmode_t)(((flag + 1) & O_ACCMODE) | \
			   (flag & __FMODE_NONOTIFY)))

#endif /* _LINUX_FS_H */
