
#ifndef _LINUX_FS_H
#define _LINUX_FS_H

#include <linux/linkage.h>
#include <linux/kdev_t.h>
#include <linux/dcache.h>
#include <linux/path.h>
#include <linux/gfp.h>
#include <linux/xarray.h>
#include <linux/capability.h>
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
/* end uapi/linux/fs.h */

struct vm_area_struct;
struct iov_iter;
struct fs_context;

extern void __init inode_init(void);
extern void __init files_init(void);

/* dead DIO/get_block callback typedefs + struct buffer_head fwd-decl removed (0-ref) */

#define MAY_EXEC 0x00000001
#define MAY_WRITE 0x00000002
#define MAY_READ 0x00000004
#define MAY_OPEN 0x00000020

#define FMODE_READ ((__force fmode_t)0x1)

#define FMODE_WRITE ((__force fmode_t)0x2)

#define FMODE_EXEC ((__force fmode_t)0x20)

#define FMODE_WRITER ((__force fmode_t)0x10000)

#define FMODE_CAN_READ ((__force fmode_t)0x20000)

#define FMODE_CAN_WRITE ((__force fmode_t)0x40000)

#define FMODE_OPENED ((__force fmode_t)0x80000)
#define FMODE_CREATED ((__force fmode_t)0x100000)

#define FMODE_NONOTIFY ((__force fmode_t)0x4000000)

/* Reduced positive_aop_returns - only AOP_TRUNCATED_PAGE used */
enum positive_aop_returns { AOP_TRUNCATED_PAGE = 0x80001 };

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

	/* writepage, writepages, dirty_folio, readahead, bmap, launder_folio,
	 * migratepage, isolate_page, putback_page, is_dirty_writeback,
	 * error_remove_page, swap_*, release_folio, free_folio, direct_IO,
	 * is_partially_uptodate removed - unused */
};

struct address_space {
	struct inode *host;
	struct xarray i_pages;
	struct rw_semaphore invalidate_lock;
	gfp_t gfp_mask;
	const struct address_space_operations *a_ops;
} __attribute__((aligned(sizeof(long)))) __randomize_layout;

struct inode {
	umode_t i_mode;
	kuid_t i_uid;
	kgid_t i_gid;
	const struct inode_operations *i_op;
	struct super_block *i_sb;
	struct address_space *i_mapping;
	dev_t i_rdev;
	loff_t i_size;
	spinlock_t i_lock;
	struct rw_semaphore i_rwsem;
	atomic_t i_writecount;
	const struct file_operations *i_fop;
	struct address_space i_data;
	struct cdev *i_cdev;
} __randomize_layout;

/* Reduced inode_i_mutex_lock_class - only I_MUTEX_PARENT used (lockdep off: value unused) */
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

static inline loff_t i_size_read(const struct inode *inode)
{
	return inode->i_size;
}

static inline void i_size_write(struct inode *inode, loff_t i_size)
{
	inode->i_size = i_size;
}

/* struct fown_struct removed - f_owner field was write-only (lock never
   acquired, pid never assigned -> put_pid(NULL) no-op) */

struct file {
	union {
		struct rcu_head fu_rcuhead;
	} f_u;
	struct path f_path;
	struct inode *f_inode;
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

/* struct file_lock removed - file_operations.lock (its sole user) was never
 * assigned or dispatched in this build (no locking syscalls reach it) */

static inline struct inode *file_inode(const struct file *f)
{
	return f->f_inode;
}

struct super_block {
	loff_t s_maxbytes;
	struct dentry *s_root;
	struct hlist_bl_head s_roots;
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

static inline kuid_t i_uid_into_mnt(struct user_namespace *mnt_userns,
				    const struct inode *inode)
{
	return mapped_kuid_fs(mnt_userns, i_user_ns(inode), inode->i_uid);
}

static inline kgid_t i_gid_into_mnt(struct user_namespace *mnt_userns,
				    const struct inode *inode)
{
	return mapped_kgid_fs(mnt_userns, i_user_ns(inode), inode->i_gid);
}

static inline void inode_fsuid_set(struct inode *inode,
				   struct user_namespace *mnt_userns)
{
	inode->i_uid = mapped_fsuid(mnt_userns, i_user_ns(inode));
}

static inline void inode_fsgid_set(struct inode *inode,
				   struct user_namespace *mnt_userns)
{
	inode->i_gid = mapped_fsgid(mnt_userns, i_user_ns(inode));
}

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

void inode_init_owner(struct user_namespace *mnt_userns, struct inode *inode,
		      const struct inode *dir, umode_t mode);

struct file_operations {
	ssize_t (*read_iter)(struct kiocb *, struct iov_iter *);
	ssize_t (*write_iter)(struct kiocb *, struct iov_iter *);
	int (*mmap)(struct file *, struct vm_area_struct *);
	int (*open)(struct inode *, struct file *);
	/* iopoll/iterate/iterate_shared/poll/unlocked_ioctl/compat_ioctl/
	 * mmap_supported_flags/flush/fsync/sendpage/check_flags/flock/
	 * splice_write/splice_read/setlease/remap_file_range/fadvise/uring_cmd/
	 * fallocate/show_fdinfo/copy_file_range/llseek/lock removed - zero ->field
	 * dispatch, bare-deref and assignment tree-wide; read/write/release
	 * removed - tested or called but never assigned (only read_iter/
	 * write_iter/mmap/open are live) */
} __randomize_layout;

struct inode_operations {
	struct dentry *(*lookup)(struct inode *, struct dentry *, unsigned int);
	int (*create)(struct user_namespace *, struct inode *, struct dentry *,
		      umode_t, bool);
	int (*mkdir)(struct user_namespace *, struct inode *, struct dentry *,
		     umode_t);
	int (*mknod)(struct user_namespace *, struct inode *, struct dentry *,
		     umode_t, dev_t);
	/* permission/readlink/link/unlink/symlink/rmdir/rename/getattr/
	 * listxattr/update_time/get_link removed - zero ->field deref or
	 * dispatched-but-never-assigned (only lookup/create/mkdir/mknod/setattr
	 * are live). get_acl/fiemap/set_acl/fileattr_set/fileattr_get - unused */
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

/* S_NOSEC, S_DAX, S_NOATIME, S_APPEND, S_IMMUTABLE, S_DEAD, S_NOCMTIME,
 * S_SWAPFILE, S_AUTOMOUNT, S_VERITY, S_KERNEL_FILE removed - never set on
 * any inode (S_NOSEC depended on SB_NOSEC which is never set), so the whole
 * i_flags field + all IS_NOSEC/IS_DAX/etc tests were statically dead. */

/* IS_APPEND, IS_IMMUTABLE, IS_DEADDIR, IS_NOCMTIME, IS_SWAPFILE,
 * IS_AUTOMOUNT, IS_NOSEC, IS_DAX removed - underlying S_* flags never set */

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

/* Removed: mount_nodev, kill_block_super - never called */

#define fops_get(fops) ((fops) ? (fops) : NULL)
#define replace_fops(f, fops)                     \
	do {                                      \
		struct file *__file = (f);        \
		BUG_ON(!(__file->f_op = (fops))); \
	} while (0)

extern int current_umask(void);

#define MAX_RW_COUNT (INT_MAX & PAGE_MASK)

struct filename {
	const char *name;
	int refcnt;
	const char iname[];
};
static_assert(offsetof(struct filename, iname) % sizeof(long) == 0);

/* vfs_fallocate, do_sys_open, file_open_name, file_open_root, dentry_open,
   dentry_create, open_with_fake_path removed - unused/internal only */
extern struct file *filp_open(const char *, int, umode_t);

extern struct filename *getname_kernel(const char *);
extern void putname(struct filename *name);

extern void __init vfs_caches_init_early(void);
extern void __init vfs_caches_init(void);

extern struct kmem_cache *names_cachep;

#define __getname() kmem_cache_alloc(names_cachep, GFP_KERNEL)
#define __putname(name) kmem_cache_free(names_cachep, (void *)(name))

extern const struct file_operations def_chr_fops;

extern void init_special_inode(struct inode *, umode_t, dev_t);

static inline ssize_t generic_write_sync(struct kiocb *iocb, ssize_t count)
{
	return count;
}

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

/* Removed: stream_open - never called */

#define special_file(m) (S_ISCHR(m) || S_ISBLK(m) || S_ISFIFO(m) || S_ISSOCK(m))

/* Removed: get_super, get_active_super, drop_super, drop_super_exclusive,
   iterate_supers, iterate_supers_type - never called */

extern const struct address_space_operations ram_aops;

extern struct dentry *simple_lookup(struct inode *, struct dentry *,
				    unsigned int flags);

#define __FMODE_EXEC ((__force int)FMODE_EXEC)
#define __FMODE_NONOTIFY ((__force int)FMODE_NONOTIFY)

#define ACC_MODE(x) ("\004\002\006\006"[(x) & O_ACCMODE])
#define OPEN_FMODE(flag)                              \
	((__force fmode_t)(((flag + 1) & O_ACCMODE) | \
			   (flag & __FMODE_NONOTIFY)))

#endif
