
#ifndef _LINUX_FS_H
#define _LINUX_FS_H

#include <linux/linkage.h>
#include <linux/wait_bit.h>
#include <linux/kdev_t.h>
#include <linux/dcache.h>
#include <linux/path.h>
#include <linux/stat.h>
#include <linux/cache.h>
#include <linux/list.h>
#include <linux/list_lru.h>
#include <linux/llist.h>
/* radix-tree.h removed - unused */
#include <linux/xarray.h>
#include <linux/rbtree.h>
#include <linux/init.h>
#include <linux/pid.h>
#include <linux/bug.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/mm_types.h>
/* capability.h removed - unused */
#include <linux/semaphore.h>
#include <linux/fcntl.h>
#include <linux/rculist_bl.h>
#include <linux/atomic.h>
#include <linux/shrinker.h>
/* enum migrate_mode, migrate_reason removed - unused */
#include <linux/uidgid.h>
#include <linux/lockdep.h>
#include <linux/percpu-rwsem.h>
#include <linux/workqueue.h>

/* Inlined from delayed_call.h */
struct delayed_call {
	void (*fn)(void *);
	void *arg;
};
static inline void set_delayed_call(struct delayed_call *call, void (*fn)(void *), void *arg)
{
	call->fn = fn;
	call->arg = arg;
}
static inline void do_delayed_call(struct delayed_call *call)
{
	if (call->fn)
		call->fn(call->arg);
}
static inline void clear_delayed_call(struct delayed_call *call)
{
	call->fn = NULL;
}

/* uuid.h removed - unused */
/* ioprio.h removed - unused after ki_ioprio removal */

/* Inlined from errseq.h */
typedef u32	errseq_t;
errseq_t errseq_set(errseq_t *eseq, int err);
/* errseq_sample removed - never called */
#include <linux/build_bug.h>

#define DT_DIR		4
#include <linux/stddef.h>
#include <linux/mount.h>
#include <linux/cred.h>
#include <linux/mnt_idmapping.h>
#include <linux/slab.h>

/* byteorder.h, ioctl.h removed - unused */
#include <linux/limits.h>
#define INR_OPEN_CUR 1024
#define INR_OPEN_MAX 4096
/* NR_FILE removed - never used */
#define SEEK_SET	0
#define SEEK_CUR	1
#define SEEK_END	2
#define SEEK_DATA	3
#define SEEK_HOLE	4
#define RENAME_NOREPLACE	(1 << 0)
/* RENAME_EXCHANGE, files_stat_struct removed - unused */
/* RWF_* values needed for IOCB_* macros, rwf_t typedef */
typedef int rwf_t;
#define RWF_NOWAIT	0x00000008
/* end uapi/linux/fs.h */

struct backing_dev_info;
struct bdi_writeback;
struct bio;
struct iovec;
struct kiocb;
struct kobject;
struct pipe_inode_info;
struct poll_table_struct;
struct kstatfs;
struct vm_area_struct;
struct vfsmount;
struct cred;
struct seq_file;
struct workqueue_struct;
struct iov_iter;
struct fs_context;
struct fs_parameter_spec;

extern void __init inode_init(void);
extern void __init inode_init_early(void);
extern void __init files_init(void);
/* files_maxfiles_init removed - was empty stub */
extern unsigned int sysctl_nr_open;

/* rwf_t defined earlier */
/* struct buffer_head, get_block_t removed - only used by block device code */

#define MAY_EXEC		0x00000001
#define MAY_WRITE		0x00000002
#define MAY_READ		0x00000004
#define MAY_ACCESS		0x00000010
#define MAY_OPEN		0x00000020
#define MAY_CHDIR		0x00000040

#define MAY_NOT_BLOCK		0x00000080

#define FMODE_READ		((__force fmode_t)0x1)

#define FMODE_WRITE		((__force fmode_t)0x2)

#define FMODE_LSEEK		((__force fmode_t)0x4)

#define FMODE_PREAD		((__force fmode_t)0x8)

#define FMODE_PWRITE		((__force fmode_t)0x10)

#define FMODE_EXEC		((__force fmode_t)0x20)
#define FMODE_UNSIGNED_OFFSET	((__force fmode_t)0x2000)

#define FMODE_PATH		((__force fmode_t)0x4000)

#define FMODE_ATOMIC_POS	((__force fmode_t)0x8000)

#define FMODE_WRITER		((__force fmode_t)0x10000)

#define FMODE_CAN_READ          ((__force fmode_t)0x20000)

#define FMODE_CAN_WRITE         ((__force fmode_t)0x40000)

#define FMODE_OPENED		((__force fmode_t)0x80000)
#define FMODE_CREATED		((__force fmode_t)0x100000)

#define FMODE_STREAM		((__force fmode_t)0x200000)

#define	FMODE_CAN_ODIRECT	((__force fmode_t)0x400000)

#define FMODE_NONOTIFY		((__force fmode_t)0x4000000)

#define FMODE_NOWAIT		((__force fmode_t)0x8000000)

#define FMODE_NEED_UNMOUNT	((__force fmode_t)0x10000000)

#define FMODE_NOACCOUNT		((__force fmode_t)0x20000000)


#define ATTR_MODE	(1 << 0)
#define ATTR_UID	(1 << 1)
#define ATTR_GID	(1 << 2)
#define ATTR_SIZE	(1 << 3)
#define ATTR_ATIME	(1 << 4)
#define ATTR_MTIME	(1 << 5)
#define ATTR_CTIME	(1 << 6)
#define ATTR_ATIME_SET	(1 << 7)
#define ATTR_MTIME_SET	(1 << 8)
#define ATTR_FORCE	(1 << 9) 
#define ATTR_KILL_SUID	(1 << 11)
#define ATTR_KILL_SGID	(1 << 12)
#define ATTR_FILE	(1 << 13)
#define ATTR_KILL_PRIV	(1 << 14)
#define ATTR_OPEN	(1 << 15) 
#define ATTR_TIMES_SET	(1 << 16)

#define WHITEOUT_DEV 0

struct iattr {
	unsigned int	ia_valid;
	umode_t		ia_mode;
	kuid_t		ia_uid;
	kgid_t		ia_gid;
	loff_t		ia_size;
	struct timespec64 ia_atime;
	struct timespec64 ia_mtime;
	struct timespec64 ia_ctime;

	
	struct file	*ia_file;
};

/* linux/quota.h removed - empty stub */

/* Reduced positive_aop_returns - only AOP_TRUNCATED_PAGE used */
enum positive_aop_returns { AOP_TRUNCATED_PAGE = 0x80001 };

struct page;
struct address_space;
struct writeback_control;

#define IOCB_NOWAIT		(__force int) RWF_NOWAIT
#define IOCB_DIRECT		(1 << 17)
#define IOCB_WAITQ		(1 << 19)
#define IOCB_NOIO		(1 << 20)

struct kiocb {
	struct file		*ki_filp;

	
	randomized_struct_fields_start

	loff_t			ki_pos;
	void (*ki_complete)(struct kiocb *iocb, long ret);
	void			*private;
	int			ki_flags;
	/* ki_ioprio removed - write-only field */
	struct wait_page_queue	*ki_waitq; 
	randomized_struct_fields_end
};

struct address_space_operations {
	/* writepage, writepages, dirty_folio, readahead, bmap removed - unused */
	int (*read_folio)(struct file *, struct folio *);
	int (*write_begin)(struct file *, struct address_space *mapping, loff_t pos,
			   unsigned len, struct page **pagep, void **fsdata);
	int (*write_end)(struct file *, struct address_space *mapping, loff_t pos,
			 unsigned len, unsigned copied, struct page *page, void *fsdata);
	/* invalidate_folio, release_folio, free_folio removed - never called */
	/* direct_IO, launder_folio, is_partially_uptodate removed - never set */
};

struct address_space {
	struct inode		*host;
	struct xarray		i_pages;
	struct rw_semaphore	invalidate_lock;
	gfp_t			gfp_mask;
	atomic_t		i_mmap_writable;
	struct rb_root_cached	i_mmap;
	struct rw_semaphore	i_mmap_rwsem;
	unsigned long		nrpages;
	const struct address_space_operations *a_ops;
	unsigned long		flags;
	errseq_t		wb_err;
	/* private_lock removed - only initialized, never used */
	struct list_head	private_list;
	/* private_data removed - only set to NULL, never used */
} __attribute__((aligned(sizeof(long)))) __randomize_layout;
	

#define PAGECACHE_TAG_DIRTY	XA_MARK_0
#define PAGECACHE_TAG_WRITEBACK	XA_MARK_1

static inline bool mapping_tagged(struct address_space *mapping, xa_mark_t tag)
{
	return xa_marked(&mapping->i_pages, tag);
}

static inline void i_mmap_lock_write(struct address_space *mapping)
{
	down_write(&mapping->i_mmap_rwsem);
}

static inline void i_mmap_unlock_write(struct address_space *mapping)
{
	up_write(&mapping->i_mmap_rwsem);
}


static inline void i_mmap_lock_read(struct address_space *mapping)
{
	down_read(&mapping->i_mmap_rwsem);
}

static inline void i_mmap_unlock_read(struct address_space *mapping)
{
	up_read(&mapping->i_mmap_rwsem);
}

/* mapping_writably_mapped removed - never called */

static inline void mapping_unmap_writable(struct address_space *mapping)
{
	atomic_dec(&mapping->i_mmap_writable);
}

static inline void mapping_allow_writable(struct address_space *mapping)
{
	atomic_inc(&mapping->i_mmap_writable);
}

/* i_size_ordered_init removed - never called */
/* struct posix_acl removed - unused */

#define IOP_FASTPERM	0x0001
#define IOP_LOOKUP	0x0002
#define IOP_NOFOLLOW	0x0004
#define IOP_XATTR	0x0008

struct inode {
	umode_t			i_mode;
	unsigned short		i_opflags;
	kuid_t			i_uid;
	kgid_t			i_gid;
	unsigned int		i_flags;

	const struct inode_operations	*i_op;
	struct super_block	*i_sb;
	struct address_space	*i_mapping;

	
	unsigned long		i_ino;
	
	union {
		const unsigned int i_nlink;
		unsigned int __i_nlink;
	};
	dev_t			i_rdev;
	loff_t			i_size;
	struct timespec64	i_atime;
	struct timespec64	i_mtime;
	spinlock_t		i_lock;
	u8			i_blkbits;

	
	unsigned long		i_state;
	struct rw_semaphore	i_rwsem;

	struct hlist_node	i_hash;
	struct list_head	i_lru;
	struct list_head	i_sb_list;
	/* i_wb_list removed - only initialized, never used */
	union {
		struct hlist_head	i_dentry;
		struct rcu_head		i_rcu;
	};
	atomic_t		i_count;
	/* i_dio_count removed - only initialized, never used */
	atomic_t		i_writecount;
	union {
		const struct file_operations	*i_fop;	
		void (*free_inode)(struct inode *);
	};
	/* i_flctx removed - never used */
	struct address_space	i_data;
	struct list_head	i_devices;
	union {
		struct cdev		*i_cdev;
		/* i_link removed - symlinks never created */
		unsigned		i_dir_seq;
	};

	/* i_generation, i_private removed - only initialized, never used */
} __randomize_layout;

struct timespec64 timestamp_truncate(struct timespec64 t, struct inode *inode);

static inline unsigned int i_blocksize(const struct inode *node)
{
	return (1 << node->i_blkbits);
}

static inline int inode_unhashed(struct inode *inode)
{
	return hlist_unhashed(&inode->i_hash);
}

/* enum inode_i_mutex_lock_class (I_MUTEX_NORMAL, I_MUTEX_PARENT) removed - never used */

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

static inline int inode_is_locked(struct inode *inode)
{
	return rwsem_is_locked(&inode->i_rwsem);
}

/* inode_lock_nested removed - never called */

static inline void filemap_invalidate_lock_shared(struct address_space *mapping)
{
	down_read(&mapping->invalidate_lock);
}

static inline int filemap_invalidate_trylock_shared(
					struct address_space *mapping)
{
	return down_read_trylock(&mapping->invalidate_lock);
}

static inline void filemap_invalidate_unlock_shared(
					struct address_space *mapping)
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

static inline unsigned iminor(const struct inode *inode)
{
	return MINOR(inode->i_rdev);
}


/* fown_struct removed - f_owner is never accessed */

struct file_ra_state {
	/* start, size, async_size, ra_pages removed - never accessed */
	unsigned int mmap_miss;
	loff_t prev_pos;
};

struct file {
	union {
		struct llist_node	fu_llist;
		struct rcu_head 	fu_rcuhead;
	} f_u;
	struct path		f_path;
	struct inode		*f_inode;	
	const struct file_operations	*f_op;

	
	spinlock_t		f_lock;
	atomic_long_t		f_count;
	unsigned int 		f_flags;
	fmode_t			f_mode;
	struct mutex		f_pos_lock;
	loff_t			f_pos;
	/* f_owner field removed - never accessed */
	const struct cred	*f_cred;
	struct file_ra_state	f_ra;

	void			*private_data;

	struct address_space	*f_mapping;
	/* f_wb_err removed - only written, never read */
} __randomize_layout
  __attribute__((aligned(4)));

static inline struct file *get_file(struct file *f)
{
	atomic_long_inc(&f->f_count);
	return f;
}
#define get_file_rcu(x) atomic_long_inc_not_zero(&(x)->f_count)
#define file_count(x)	atomic_long_read(&(x)->f_count)

#define	MAX_NON_LFS	((1UL<<31) - 1)

/* 32-bit only kernel */
#define MAX_LFS_FILESIZE	((loff_t)ULONG_MAX << PAGE_SHIFT)


typedef void *fl_owner_t;

struct file_lock;
/* struct net forward declaration removed - unused in fs.h */
/* locks_inode macro removed - unused */

struct files_struct;
static inline struct inode *file_inode(const struct file *f)
{
	return f->f_inode;
}

/* fasync_struct fields reduced - only forward declaration needed for pointer */
struct fasync_struct;

/* fasync_helper, kill_fasync, __f_setown removed - empty stubs */

#define SB_RDONLY	 1
#define SB_SYNCHRONOUS	16
#define SB_MANDLOCK	64
#define SB_DIRSYNC	128
#define SB_SILENT	32768
#define SB_POSIXACL	(1<<16)
#define SB_KERNMOUNT	(1<<22)
#define SB_I_VERSION	(1<<23)
#define SB_LAZYTIME	(1<<25)
/* SB_SUBMOUNT removed - unused */
#define SB_BORN		(1<<29)
#define SB_ACTIVE	(1<<30)
#define SB_NOUSER	(1<<31)
/* MNT_FORCE, MNT_DETACH, UMOUNT_NOFOLLOW removed - never used */
#define SB_I_NOEXEC	0x00000002
#define SB_I_NODEV	0x00000004
#define SB_I_USERNS_VISIBLE		0x00000010
#define SB_I_PERSB_BDI	0x00000200

enum {
	SB_UNFROZEN = 0,		
	SB_FREEZE_WRITE	= 1,		
	SB_FREEZE_PAGEFAULT = 2,	
	SB_FREEZE_FS = 3,		
	SB_FREEZE_COMPLETE = 4,		
};

#define SB_FREEZE_LEVELS (SB_FREEZE_COMPLETE - 1)

struct sb_writers {
	int				frozen;		
	wait_queue_head_t		wait_unfrozen;	
	struct percpu_rw_semaphore	rw_sem[SB_FREEZE_LEVELS];
};

struct super_block {
	struct list_head	s_list;		
	dev_t			s_dev;		
	unsigned char		s_blocksize_bits;
	unsigned long		s_blocksize;
	loff_t			s_maxbytes;	
	struct file_system_type	*s_type;
	const struct super_operations	*s_op;
	unsigned long		s_flags;
	unsigned long		s_iflags;	
	unsigned long		s_magic;
	struct dentry		*s_root;
	struct rw_semaphore	s_umount;
	int			s_count;
	atomic_t		s_active;
	const struct xattr_handler **s_xattr;
	struct hlist_bl_head	s_roots;
	struct list_head	s_mounts;
	struct backing_dev_info *s_bdi;
	struct hlist_node	s_instances;

	struct sb_writers	s_writers;

	
	void			*s_fs_info;	

	
	u32			s_time_gran;
	
	time64_t		   s_time_min;
	time64_t		   s_time_max;

	char			s_id[32];
	/* s_max_links, s_subtype removed - unused */
	struct mutex s_vfs_rename_mutex;

	const struct dentry_operations *s_d_op; 

	struct shrinker s_shrink;	


	atomic_long_t s_remove_count;

	/* s_readonly_remount, s_dio_done_wq removed - never written/created */
	struct hlist_head s_pins;

	
	struct user_namespace *s_user_ns;

	
	struct list_lru		s_dentry_lru;
	struct list_lru		s_inode_lru;
	struct rcu_head		rcu;
	struct work_struct	destroy_work;

	/* s_sync_lock removed - init-only, never locked */

	spinlock_t		s_inode_list_lock ____cacheline_aligned_in_smp;
	struct list_head	s_inodes;
	/* s_inode_wblist_lock, s_inodes_wb removed - init-only, never accessed */
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

/* fsuidgid_has_mapping removed - only caller was may_create */

extern struct timespec64 current_time(struct inode *inode);

static inline void __sb_end_write(struct super_block *sb, int level)
{
	percpu_up_read(sb->s_writers.rw_sem + level-1);
}

static inline void __sb_start_write(struct super_block *sb, int level)
{
	percpu_down_read(sb->s_writers.rw_sem + level - 1);
}

static inline void sb_end_write(struct super_block *sb)
{
	__sb_end_write(sb, SB_FREEZE_WRITE);
}

static inline void sb_end_pagefault(struct super_block *sb)
{
	__sb_end_write(sb, SB_FREEZE_PAGEFAULT);
}

static inline void sb_start_write(struct super_block *sb)
{
	__sb_start_write(sb, SB_FREEZE_WRITE);
}

static inline void sb_start_pagefault(struct super_block *sb)
{
	__sb_start_write(sb, SB_FREEZE_PAGEFAULT);
}

bool inode_owner_or_capable(struct user_namespace *mnt_userns,
			    const struct inode *inode);

/* vfs_mkdir, vfs_mknod, vfs_symlink, vfs_link, vfs_fchown, vfs_fchmod removed - never called */

void inode_init_owner(struct user_namespace *mnt_userns, struct inode *inode,
		      const struct inode *dir, umode_t mode);

/* dir_context, filldir_t removed - iterate_shared removed from file_operations */

struct file_operations {
	struct module *owner;
	/* llseek removed - lseek syscall returns ENOSYS */
	ssize_t (*read)(struct file *, char __user *, size_t, loff_t *);
	ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *);
	ssize_t (*read_iter)(struct kiocb *, struct iov_iter *);
	ssize_t (*write_iter)(struct kiocb *, struct iov_iter *);
	/* iterate, iterate_shared, sendpage, flock, fallocate, show_fdinfo, copy_file_range removed */
	/* iterate_shared removed - getdents syscalls return 0 */
	/* poll removed - syscall returns ENOSYS */
	/* unlocked_ioctl, compat_ioctl removed - syscall returns ENOTTY */
	int (*mmap)(struct file *, struct vm_area_struct *);
	unsigned long mmap_supported_flags;
	int (*open)(struct inode *, struct file *);
	/* flush, fsync, fasync, lock removed - never set/called */
	int (*release)(struct inode *, struct file *);
	unsigned long (*get_unmapped_area)(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);
	/* splice_write, splice_read removed - syscall returns ENOSYS */
} __randomize_layout;

struct inode_operations {
	struct dentry *(*lookup)(struct inode *, struct dentry *, unsigned int);
	const char *(*get_link)(struct dentry *, struct inode *, struct delayed_call *);
	int (*permission)(struct user_namespace *, struct inode *, int);
	/* get_acl, readlink removed - never set/called */

	int (*create) (struct user_namespace *, struct inode *,struct dentry *,
		       umode_t, bool);
	/* link, unlink, symlink, mkdir, rmdir, mknod, rename removed - syscalls return ENOSYS */
	int (*setattr) (struct user_namespace *, struct dentry *,
			struct iattr *);
	/* getattr, listxattr, update_time, tmpfile removed - never called */
	/* fiemap, atomic_open, set_acl, fileattr_set, fileattr_get removed - unused */
} ____cacheline_aligned;
static inline ssize_t call_write_iter(struct file *file, struct kiocb *kio, struct iov_iter *iter)
{
	return file->f_op->write_iter(kio, iter);
}

static inline int call_mmap(struct file *file, struct vm_area_struct *vma)
{
	return file->f_op->mmap(file, vma);
}

extern ssize_t vfs_write(struct file *, const char __user *, size_t, loff_t *);

struct super_operations {
	struct inode *(*alloc_inode)(struct super_block *sb);
	void (*destroy_inode)(struct inode *);
	void (*free_inode)(struct inode *);
	/* dirty_inode, write_inode removed - never set/called */
	int (*drop_inode)(struct inode *);
	void (*evict_inode)(struct inode *);
	void (*put_super)(struct super_block *);
	/* sync_fs, freeze_super, freeze_fs, thaw_super, unfreeze_fs removed */
	/* statfs removed - statfs syscalls return ENOSYS */
	/* remount_fs, umount_begin, show_options removed - never called */
};

#define S_APPEND	(1 << 2)
#define S_IMMUTABLE	(1 << 3)
#define S_DEAD		(1 << 4)
#define S_SWAPFILE	(1 << 8)
#define S_PRIVATE	(1 << 9)
#define S_AUTOMOUNT	(1 << 11)
#define S_NOSEC		(1 << 12)
#define S_DAX		0 

#define __IS_FLG(inode, flg)	((inode)->i_sb->s_flags & (flg))

static inline bool sb_rdonly(const struct super_block *sb) { return sb->s_flags & SB_RDONLY; }
#define IS_APPEND(inode)	((inode)->i_flags & S_APPEND)
#define IS_IMMUTABLE(inode)	((inode)->i_flags & S_IMMUTABLE)
#define IS_POSIXACL(inode)	__IS_FLG(inode, SB_POSIXACL)
#define IS_DEADDIR(inode)	((inode)->i_flags & S_DEAD)
#define IS_SWAPFILE(inode)	((inode)->i_flags & S_SWAPFILE)
#define IS_AUTOMOUNT(inode)	((inode)->i_flags & S_AUTOMOUNT)
#define IS_DAX(inode)		((inode)->i_flags & S_DAX)

static inline bool HAS_UNMAPPED_ID(struct user_namespace *mnt_userns,
				   struct inode *inode)
{
	return !uid_valid(i_uid_into_mnt(mnt_userns, inode)) ||
	       !gid_valid(i_gid_into_mnt(mnt_userns, inode));
}

static inline int iocb_flags(struct file *file);

static inline void init_sync_kiocb(struct kiocb *kiocb, struct file *filp)
{
	*kiocb = (struct kiocb) {
		.ki_filp = filp,
		.ki_flags = iocb_flags(filp),
		/* ki_ioprio removed - write-only field */
	};
}

#define I_DIRTY_SYNC		(1 << 0)
#define I_DIRTY_DATASYNC	(1 << 1)
#define I_DIRTY_PAGES		(1 << 2)
#define __I_NEW			3
#define I_NEW			(1 << __I_NEW)
#define I_WILL_FREE		(1 << 4)
#define I_FREEING		(1 << 5)
#define I_CLEAR			(1 << 6)
#define __I_SYNC		7
#define I_SYNC			(1 << __I_SYNC)
#define I_REFERENCED		(1 << 8)
#define I_LINKABLE		(1 << 10)
#define I_DIRTY_TIME		(1 << 11)
#define I_DONTCACHE		(1 << 16)

#define I_DIRTY_INODE (I_DIRTY_SYNC | I_DIRTY_DATASYNC)
#define I_DIRTY (I_DIRTY_INODE | I_DIRTY_PAGES)
#define I_DIRTY_ALL (I_DIRTY | I_DIRTY_TIME)

/* __mark_inode_dirty, mark_inode_dirty removed - empty stubs, no callers */

extern void inc_nlink(struct inode *inode);
extern void drop_nlink(struct inode *inode);
extern void clear_nlink(struct inode *inode);

static inline void inode_dec_link_count(struct inode *inode)
{
	drop_nlink(inode);
	/* mark_inode_dirty removed - __mark_inode_dirty is empty stub */
}

/* touch_atime, file_accessed removed - empty stubs, no callers */

struct file_system_type {
	const char *name;
	int fs_flags;
#define FS_REQUIRES_DEV		1
/* FS_BINARY_MOUNTDATA removed - unused */
#define FS_HAS_SUBTYPE		4
#define FS_USERNS_MOUNT		8
	int (*init_fs_context)(struct fs_context *);
	const struct fs_parameter_spec *parameters;
	struct dentry *(*mount) (struct file_system_type *, int,
		       const char *, void *);
	void (*kill_sb) (struct super_block *);
	struct module *owner;
	struct file_system_type * next;
	struct hlist_head fs_supers;

	struct lock_class_key s_lock_key;
	struct lock_class_key s_umount_key;
	struct lock_class_key s_vfs_rename_key;
	struct lock_class_key s_writers_key[SB_FREEZE_LEVELS];

	struct lock_class_key i_lock_key;
	struct lock_class_key i_mutex_key;
	struct lock_class_key invalidate_lock_key;
};

/* Removed: mount_nodev, kill_block_super - never called */
void generic_shutdown_super(struct super_block *sb);
void kill_anon_super(struct super_block *sb);
void kill_litter_super(struct super_block *sb);
void deactivate_super(struct super_block *sb);
void deactivate_locked_super(struct super_block *sb);
int set_anon_super(struct super_block *s, void *data);
int set_anon_super_fc(struct super_block *s, struct fs_context *fc);
int get_anon_bdev(dev_t *);
void free_anon_bdev(dev_t);
struct super_block *sget_fc(struct fs_context *fc,
			    int (*test)(struct super_block *, struct fs_context *),
			    int (*set)(struct super_block *, struct fs_context *));
/* sget removed - never called */

#define fops_get(fops) \
	(((fops) && try_module_get((fops)->owner) ? (fops) : NULL))
#define fops_put(fops) \
	do { if (fops) module_put((fops)->owner); } while(0)

#define replace_fops(f, fops) \
	do {	\
		struct file *__file = (f); \
		fops_put(__file->f_op); \
		BUG_ON(!(__file->f_op = (fops))); \
	} while(0)

extern int register_filesystem(struct file_system_type *);

extern int current_umask(void);

extern void ihold(struct inode * inode);
extern void iput(struct inode *);

#define MAX_RW_COUNT (INT_MAX & PAGE_MASK)

struct audit_names;
struct filename {
	const char		*name;	
	const __user char	*uptr;	
	int			refcnt;
	struct audit_names	*aname;
	const char		iname[];
};
static_assert(offsetof(struct filename, iname) % sizeof(long) == 0);

static inline struct user_namespace *file_mnt_user_ns(struct file *file)
{
	return mnt_user_ns(file->f_path.mnt);
}

/* vfs_truncate removed - never called */
int do_truncate(struct user_namespace *, struct dentry *, loff_t start,
		unsigned int time_attrs, struct file *filp);
/* vfs_fallocate, do_sys_open, file_open_name, file_open_root, dentry_open,
   dentry_create, open_with_fake_path removed - unused/internal only */
extern struct file *filp_open(const char *, int, umode_t);
extern int filp_close(struct file *, fl_owner_t id);

/* getname_flags removed - never called */
/* getname removed - never called */
extern struct filename *getname_kernel(const char *);
extern void putname(struct filename *name);

/* finish_open removed - never called */

extern void __init vfs_caches_init_early(void);
extern void __init vfs_caches_init(void);

extern struct kmem_cache *names_cachep;

#define __getname()		kmem_cache_alloc(names_cachep, GFP_KERNEL)
#define __putname(name)		kmem_cache_free(names_cachep, (void *)(name))

extern const struct file_operations def_blk_fops;
extern const struct file_operations def_chr_fops;

#define CHRDEV_MAJOR_MAX 512

#define CHRDEV_MAJOR_DYN_END 234

#define CHRDEV_MAJOR_DYN_EXT_START 511
#define CHRDEV_MAJOR_DYN_EXT_END 384

extern int alloc_chrdev_region(dev_t *, unsigned, unsigned, const char *);
extern int register_chrdev_region(dev_t, unsigned, const char *);
/* __register_chrdev, __unregister_chrdev, register_chrdev removed - never called */
extern void unregister_chrdev_region(dev_t, unsigned);

extern void init_special_inode(struct inode *, umode_t, dev_t);

/* file_check_and_advance_wb_err, file_write_and_wait_range removed - never called */
static inline ssize_t generic_write_sync(struct kiocb *iocb, ssize_t count)
{
	return count;
}


int notify_change(struct user_namespace *, struct dentry *,
		  struct iattr *, struct inode **);
int inode_permission(struct user_namespace *, struct inode *, int);
int generic_permission(struct user_namespace *, struct inode *, int);
static inline int path_permission(const struct path *path, int mask)
{
	return inode_permission(mnt_user_ns(path->mnt),
				d_inode(path->dentry), mask);
}

static inline void file_start_write(struct file *file)
{
	if (!S_ISREG(file_inode(file)->i_mode))
		return;
	sb_start_write(file_inode(file)->i_sb);
}

static inline void file_end_write(struct file *file)
{
	if (!S_ISREG(file_inode(file)->i_mode))
		return;
	__sb_end_write(file_inode(file)->i_sb, SB_FREEZE_WRITE);
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
static inline void put_write_access(struct inode * inode)
{
	atomic_dec(&inode->i_writecount);
}
static inline void allow_write_access(struct file *file)
{
	if (file)
		atomic_inc(&file_inode(file)->i_writecount);
}
extern ssize_t kernel_read(struct file *, void *, size_t, loff_t *);
ssize_t __kernel_read(struct file *file, void *buf, size_t count, loff_t *pos);
/* kernel_write and __kernel_write removed - never called */
extern struct file * open_exec(const char *);

extern bool is_subdir(struct dentry *, struct dentry *);
/* vfs_llseek removed - never called */
extern int inode_init_always(struct super_block *, struct inode *);
extern void inode_init_once(struct inode *);
extern int generic_delete_inode(struct inode *inode);
static inline int generic_drop_inode(struct inode *inode)
{
	return !inode->i_nlink || inode_unhashed(inode);
}
extern unsigned int get_next_ino(void);
extern void evict_inodes(struct super_block *sb);

extern void clear_inode(struct inode *);
extern void __destroy_inode(struct inode *);
extern struct inode *new_inode_pseudo(struct super_block *sb);
extern struct inode *new_inode(struct super_block *sb);

static inline void *
alloc_inode_sb(struct super_block *sb, struct kmem_cache *cache, gfp_t gfp)
{
	return kmem_cache_alloc_lru(cache, &sb->s_inode_lru, gfp);
}

extern void __remove_inode_hash(struct inode *);
static inline void remove_inode_hash(struct inode *inode)
{
	if (!inode_unhashed(inode) && !hlist_fake(&inode->i_hash))
		__remove_inode_hash(inode);
}

extern void inode_sb_list_add(struct inode *inode);
extern void inode_add_lru(struct inode *inode);

extern int generic_file_mmap(struct file *, struct vm_area_struct *);
extern ssize_t generic_write_checks(struct kiocb *, struct iov_iter *);
ssize_t filemap_read(struct kiocb *iocb, struct iov_iter *to,
		ssize_t already_read);
extern ssize_t generic_file_read_iter(struct kiocb *, struct iov_iter *);
extern ssize_t __generic_file_write_iter(struct kiocb *, struct iov_iter *);
extern ssize_t generic_file_write_iter(struct kiocb *, struct iov_iter *);
extern ssize_t generic_file_direct_write(struct kiocb *, struct iov_iter *);
ssize_t generic_perform_write(struct kiocb *, struct iov_iter *);

extern ssize_t generic_file_splice_read(struct file *, loff_t *,
		struct pipe_inode_info *, size_t, unsigned int);
extern ssize_t iter_file_splice_write(struct pipe_inode_info *,
		struct file *, loff_t *, size_t, unsigned int);

/* llseek functions removed - llseek callback removed from file_operations */
/* rw_verify_area removed - no callers */
extern int nonseekable_open(struct inode * inode, struct file * filp);
/* Removed: stream_open - never called */

#define special_file(m) (S_ISCHR(m)||S_ISBLK(m)||S_ISFIFO(m)||S_ISSOCK(m))

/* page_get_link, page_put_link, page_symlink, page_symlink_inode_operations
 * removed - symlinks never created */
/* generic_fillattr removed - empty stub, callers simplified */
/* vfs_getattr removed - always returned 0, caller simplified */

extern struct file_system_type *get_filesystem(struct file_system_type *fs);
extern void put_filesystem(struct file_system_type *fs);
extern struct file_system_type *get_fs_type(const char *name);
/* Removed: get_super, get_active_super, drop_super_exclusive,
   iterate_supers, iterate_supers_type, drop_super - never called */

extern int dcache_dir_open(struct inode *, struct file *);
extern int dcache_dir_close(struct inode *, struct file *);
/* dcache_dir_lseek, dcache_readdir removed - iterate_shared removed */
extern int simple_setattr(struct user_namespace *, struct dentry *,
			  struct iattr *);
/* simple_getattr, simple_statfs removed - callbacks removed */
/* simple_link, simple_unlink, simple_rmdir, simple_rename removed - syscalls return ENOSYS */
extern int noop_fsync(struct file *, loff_t, loff_t, int);
extern int simple_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata);
extern const struct address_space_operations ram_aops;
extern int always_delete_dentry(const struct dentry *);
/* alloc_anon_inode removed - never called */
extern const struct dentry_operations simple_dentry_operations;

extern struct dentry *simple_lookup(struct inode *, struct dentry *, unsigned int flags);
extern ssize_t generic_read_dir(struct file *, char __user *, size_t, loff_t *);
extern const struct file_operations simple_dir_operations;
extern bool is_empty_dir_inode(struct inode *inode);
/* simple_pin_fs, simple_release_fs removed - never called */
/* may_setattr, inode_newsize_ok removed - always returned 0 */
int setattr_prepare(struct user_namespace *, struct dentry *, struct iattr *);
void setattr_copy(struct user_namespace *, struct inode *inode,
		  const struct iattr *attr);

/* file_update_time removed - callers inlined */

/* vma_is_fsdax removed - never called */

static inline int iocb_flags(struct file *file)
{
	int res = 0;
	if (file->f_flags & O_DIRECT)
		res |= IOCB_DIRECT;
	return res;
}


static inline ino_t parent_ino(struct dentry *dentry)
{
	ino_t res;
	spin_lock(&dentry->d_lock);
	res = dentry->d_parent->d_inode->i_ino;
	spin_unlock(&dentry->d_lock);
	return res;
}


int __init list_bdev_fs_names(char *buf, size_t size);

#define __FMODE_EXEC		((__force int) FMODE_EXEC)
#define __FMODE_NONOTIFY	((__force int) FMODE_NONOTIFY)

#define ACC_MODE(x) ("\004\002\006\006"[(x)&O_ACCMODE])
#define OPEN_FMODE(flag) ((__force fmode_t)(((flag + 1) & O_ACCMODE) | \
					    (flag & __FMODE_NONOTIFY)))

static inline bool is_sxid(umode_t mode)
{
	return (mode & S_ISUID) || ((mode & S_ISGID) && (mode & S_IXGRP));
}
/* dir_emit* functions removed - iterate_shared removed from file_operations */
extern bool path_noexec(const struct path *path);
extern void inode_nohighmem(struct inode *inode);

#endif
