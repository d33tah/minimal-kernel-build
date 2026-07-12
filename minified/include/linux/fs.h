
#ifndef _LINUX_FS_H
#define _LINUX_FS_H

#include <linux/linkage.h>
#include <linux/kdev_t.h>
#include <linux/dcache.h>
#include <linux/path.h>
#include <linux/stat.h>
#include <linux/list_lru.h>
#include <linux/llist.h>
#include <linux/radix-tree.h>
#include <linux/rbtree.h>
#include <linux/pid.h>
#include <linux/rwsem.h>
#include <linux/mm_types.h>
#include <linux/capability.h>
#include <linux/semaphore.h>
#include <linux/fcntl.h>

#include <linux/uuid.h>


#include <linux/mount.h>
#include <linux/cred.h>
#include <linux/mnt_idmapping.h>
#include <linux/slab.h>

#include <asm/byteorder.h>
#include <linux/limits.h>
#define INR_OPEN_CUR 1024
#define INR_OPEN_MAX 4096
#define SEEK_END	2
/* RWF_* values needed for IOCB_* macros */
#define RWF_NOWAIT	0x00000008
/* end uapi/linux/fs.h */

struct vm_area_struct;
struct cred;
struct iov_iter;
struct fs_context;

extern void __init inode_init(void);
extern void __init files_init(void);
extern unsigned int sysctl_nr_open;

/* dead DIO/get_block callback typedefs + struct buffer_head fwd-decl removed (0-ref) */

#define MAY_EXEC		0x00000001
#define MAY_WRITE		0x00000002
#define MAY_READ		0x00000004
#define MAY_APPEND		0x00000008
#define MAY_OPEN		0x00000020

#define MAY_NOT_BLOCK		0x00000080

#define FMODE_READ		((__force fmode_t)0x1)

#define FMODE_WRITE		((__force fmode_t)0x2)

#define FMODE_EXEC		((__force fmode_t)0x20)

#define FMODE_PATH		((__force fmode_t)0x4000)

#define FMODE_ATOMIC_POS	((__force fmode_t)0x8000)

#define FMODE_WRITER		((__force fmode_t)0x10000)

#define FMODE_CAN_READ          ((__force fmode_t)0x20000)

#define FMODE_CAN_WRITE         ((__force fmode_t)0x40000)

#define FMODE_OPENED		((__force fmode_t)0x80000)
#define FMODE_CREATED		((__force fmode_t)0x100000)

#define FMODE_NONOTIFY		((__force fmode_t)0x4000000)


#define ATTR_MODE	(1 << 0)
#define ATTR_SIZE	(1 << 3)
#define ATTR_ATIME	(1 << 4)
#define ATTR_MTIME	(1 << 5)
#define ATTR_CTIME	(1 << 6)
#define ATTR_FORCE	(1 << 9)
#define ATTR_KILL_SUID	(1 << 11)
#define ATTR_KILL_SGID	(1 << 12)

#define WHITEOUT_DEV 0

struct iattr {
	unsigned int	ia_valid;
	umode_t		ia_mode;
	loff_t		ia_size;
	struct timespec64 ia_atime, ia_mtime, ia_ctime;
};

/* Reduced positive_aop_returns - only AOP_TRUNCATED_PAGE used */
enum positive_aop_returns { AOP_TRUNCATED_PAGE = 0x80001 };

struct page;
struct address_space;

#define IOCB_NOWAIT		(__force int) RWF_NOWAIT
#define IOCB_WAITQ		(1 << 19)
#define IOCB_NOIO		(1 << 20)

struct kiocb {
	struct file		*ki_filp;

	
	randomized_struct_fields_start

	loff_t			ki_pos;
	int			ki_flags;
	randomized_struct_fields_end
};

struct address_space_operations {
	int (*read_folio)(struct file *, struct folio *);

	int (*write_begin)(struct file *, struct address_space *mapping,
				loff_t pos, unsigned len,
				struct page **pagep, void **fsdata);
	int (*write_end)(struct file *, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned copied,
				struct page *page, void *fsdata);

	/* writepage, writepages, dirty_folio, readahead, bmap, launder_folio,
	 * migratepage, isolate_page, putback_page, is_dirty_writeback,
	 * error_remove_page, swap_*, release_folio, free_folio, direct_IO,
	 * is_partially_uptodate removed - unused */
};

struct address_space {
	struct inode		*host;
	struct xarray		i_pages;
	struct rw_semaphore	invalidate_lock;
	gfp_t			gfp_mask;
	atomic_t		i_mmap_writable;
	struct rb_root_cached	i_mmap;
	struct rw_semaphore	i_mmap_rwsem;
	const struct address_space_operations *a_ops;
	unsigned long		flags;
} __attribute__((aligned(sizeof(long)))) __randomize_layout;
	



static inline void i_mmap_lock_write(struct address_space *mapping)
{
	down_write(&mapping->i_mmap_rwsem);
}

static inline void i_mmap_unlock_write(struct address_space *mapping)
{
	up_write(&mapping->i_mmap_rwsem);
}


/* i_mmap_lock_read / i_mmap_unlock_read removed: 0-caller static-inline orphans */

static inline int mapping_writably_mapped(struct address_space *mapping)
{
	return atomic_read(&mapping->i_mmap_writable) > 0;
}

static inline void mapping_allow_writable(struct address_space *mapping)
{
	atomic_inc(&mapping->i_mmap_writable);
}

#define i_size_ordered_init(inode) do { } while (0)

#define IOP_LOOKUP	0x0002
#define IOP_NOFOLLOW	0x0004

struct inode {
	umode_t			i_mode;
	unsigned short		i_opflags;
	kuid_t			i_uid;
	kgid_t			i_gid;

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
	struct timespec64 i_atime, i_mtime, i_ctime;
	spinlock_t		i_lock;
	u8			i_blkbits;


	struct rw_semaphore	i_rwsem;

	struct hlist_node	i_hash;
	struct list_head	i_lru;
	union {
		struct hlist_head	i_dentry;
		struct rcu_head		i_rcu;
	};
	atomic_t		i_count;
	atomic_t		i_writecount;
	const struct file_operations	*i_fop;
	struct address_space	i_data;
	struct list_head	i_devices;
	union {
		struct cdev		*i_cdev;
		unsigned		i_dir_seq;
	};

} __randomize_layout;

struct timespec64 timestamp_truncate(struct timespec64 t, struct inode *inode);

static inline unsigned int i_blocksize(const struct inode *node)
{
	return (1 << node->i_blkbits);
}

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

static inline int inode_is_locked(struct inode *inode)
{
	return rwsem_is_locked(&inode->i_rwsem);
}

static inline void inode_lock_nested(struct inode *inode, unsigned subclass)
{
	down_write_nested(&inode->i_rwsem, subclass);
}

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



/* struct fown_struct removed - f_owner field was write-only (lock never
   acquired, pid never assigned -> put_pid(NULL) no-op) */

struct file_ra_state {
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


	atomic_long_t		f_count;
	unsigned int 		f_flags;
	fmode_t			f_mode;
	struct mutex		f_pos_lock;
	loff_t			f_pos;
	const struct cred	*f_cred;
	struct file_ra_state	f_ra;

	void			*private_data;

	struct address_space	*f_mapping;
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

/* struct file_lock removed - file_operations.lock (its sole user) was never
 * assigned or dispatched in this build (no locking syscalls reach it) */

static inline struct inode *file_inode(const struct file *f)
{
	return f->f_inode;
}

#define SB_RDONLY	 1
#define SB_SYNCHRONOUS	16
#define SB_MANDLOCK	64
#define SB_DIRSYNC	128
#define SB_NOATIME	1024
#define SB_KERNMOUNT	(1<<22)
#define SB_LAZYTIME	(1<<25)
#define SB_ACTIVE	(1<<30)

#define SB_I_NOEXEC	0x00000002
#define SB_I_NODEV	0x00000004

enum {
	SB_FREEZE_WRITE	= 1,
};

struct super_block {
	unsigned char		s_blocksize_bits;
	loff_t			s_maxbytes;
	struct file_system_type	*s_type;
	const struct super_operations	*s_op;
	unsigned long s_flags, s_iflags;
	struct dentry		*s_root;
	struct rw_semaphore	s_umount;
	atomic_t		s_active;
	struct hlist_bl_head	s_roots;

	void			*s_fs_info;

	
	u32			s_time_gran;
	
	time64_t		   s_time_min;
	time64_t		   s_time_max;

	char			s_id[32];


	struct user_namespace *s_user_ns;

	
	struct list_lru s_dentry_lru, s_inode_lru;
	struct rcu_head		rcu;
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

extern struct timespec64 current_time(struct inode *inode);

static inline void __sb_end_write(struct super_block *sb, int level)
{
}

static inline void __sb_start_write(struct super_block *sb, int level)
{
}

static inline void sb_end_write(struct super_block *sb)
{
	__sb_end_write(sb, SB_FREEZE_WRITE);
}

static inline void sb_start_write(struct super_block *sb)
{
	__sb_start_write(sb, SB_FREEZE_WRITE);
}

bool inode_owner_or_capable(struct user_namespace *mnt_userns,
			    const struct inode *inode);

int vfs_mkdir(struct user_namespace *, struct inode *,
	      struct dentry *, umode_t);
int vfs_mknod(struct user_namespace *, struct inode *, struct dentry *,
              umode_t, dev_t);
int vfs_symlink(struct user_namespace *, struct inode *,
		struct dentry *, const char *);
int vfs_link(struct dentry *, struct user_namespace *, struct inode *,
	     struct dentry *, struct inode **);

int vfs_fchown(struct file *file, uid_t user, gid_t group);
int vfs_fchmod(struct file *file, umode_t mode);

void inode_init_owner(struct user_namespace *mnt_userns, struct inode *inode,
		      const struct inode *dir, umode_t mode);

struct file_operations {
	ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);
	ssize_t (*write) (struct file *, const char __user *, size_t, loff_t *);
	ssize_t (*read_iter) (struct kiocb *, struct iov_iter *);
	ssize_t (*write_iter) (struct kiocb *, struct iov_iter *);
	int (*mmap) (struct file *, struct vm_area_struct *);
	int (*open) (struct inode *, struct file *);
	int (*release) (struct inode *, struct file *);
	unsigned long (*get_unmapped_area)(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);
	/* iopoll/iterate/iterate_shared/poll/unlocked_ioctl/compat_ioctl/
	 * mmap_supported_flags/flush/fsync/sendpage/check_flags/flock/
	 * splice_write/splice_read/setlease/remap_file_range/fadvise/uring_cmd/
	 * fallocate/show_fdinfo/copy_file_range/llseek/lock removed - zero ->field
	 * dispatch, bare-deref and assignment tree-wide (only read/write/
	 * read_iter/write_iter/mmap/open/release/fasync/get_unmapped_area
	 * are live; their syscall consumers were all excised earlier) */
} __randomize_layout;

struct inode_operations {
	struct dentry * (*lookup) (struct inode *,struct dentry *, unsigned int);
	int (*create) (struct user_namespace *, struct inode *,struct dentry *,
		       umode_t, bool);
	int (*mkdir) (struct user_namespace *, struct inode *,struct dentry *,
		      umode_t);
	int (*mknod) (struct user_namespace *, struct inode *,struct dentry *,
		      umode_t,dev_t);
	int (*setattr) (struct user_namespace *, struct dentry *,
			struct iattr *);
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

extern ssize_t vfs_write(struct file *, const char __user *, size_t, loff_t *);

struct super_operations {
	/* All fields removed: none are dispatched in this build. drop_inode was
	 * the last one (assigned by ramfs_ops to generic_delete_inode) but there
	 * is no ->drop_inode() call site, so it was write-only - removed. The
	 * earlier set (alloc_inode, destroy_inode, free_inode, dirty_inode,
	 * write_inode, evict_inode, put_super, sync_fs, statfs, remount_fs,
	 * umount_begin, show_options, freeze/show_devname/show_path/show_stats)
	 * had zero dispatch and zero assignment - removed previously. */
};

/* S_NOSEC, S_DAX, S_NOATIME, S_APPEND, S_IMMUTABLE, S_DEAD, S_NOCMTIME,
 * S_SWAPFILE, S_AUTOMOUNT, S_VERITY, S_KERNEL_FILE removed - never set on
 * any inode (S_NOSEC depended on SB_NOSEC which is never set), so the whole
 * i_flags field + all IS_NOSEC/IS_DAX/etc tests were statically dead. */

#define __IS_FLG(inode, flg)	((inode)->i_sb->s_flags & (flg))

static inline bool sb_rdonly(const struct super_block *sb) { return sb->s_flags & SB_RDONLY; }
#define IS_NOATIME(inode)	__IS_FLG(inode, SB_RDONLY|SB_NOATIME)
/* IS_APPEND, IS_IMMUTABLE, IS_DEADDIR, IS_NOCMTIME, IS_SWAPFILE,
 * IS_AUTOMOUNT, IS_NOSEC, IS_DAX removed - underlying S_* flags never set */

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
	};
}

static inline void mark_inode_dirty(struct inode *inode)
{
}

extern void inc_nlink(struct inode *inode);

enum file_time_flags {
	S_ATIME = 1,
	S_MTIME = 2,
	S_CTIME = 4,
};

extern bool atime_needs_update(const struct path *, struct inode *);
extern void touch_atime(const struct path *);
int inode_update_time(struct inode *inode, struct timespec64 *time, int flags);

static inline void file_accessed(struct file *file)
{
	if (!(file->f_flags & O_NOATIME))
		touch_atime(&file->f_path);
}


struct file_system_type {
	const char *name;
	int fs_flags;
#define FS_USERNS_MOUNT		8
	int (*init_fs_context)(struct fs_context *);
	void (*kill_sb) (struct super_block *);
	struct module *owner;
	struct file_system_type * next;
};

/* Removed: mount_nodev, kill_block_super - never called */
void kill_litter_super(struct super_block *sb);
void deactivate_super(struct super_block *sb);
void deactivate_locked_super(struct super_block *sb);
int set_anon_super_fc(struct super_block *s, struct fs_context *fc);
struct super_block *sget_fc(struct fs_context *fc,
			    int (*set)(struct super_block *, struct fs_context *));

#define fops_get(fops) 	((fops) ? (fops) : NULL)
#define fops_put(fops) 	do { (void)(fops); } while(0)

#define replace_fops(f, fops) 	do {			struct file *__file = (f); 		fops_put(__file->f_op); 		BUG_ON(!(__file->f_op = (fops))); 	} while(0)

extern int register_filesystem(struct file_system_type *);

extern int current_umask(void);

extern void iput(struct inode *);

#define MAX_RW_COUNT (INT_MAX & PAGE_MASK)

struct filename {
	const char		*name;
	const __user char	*uptr;
	int			refcnt;
	const char		iname[];
};
static_assert(offsetof(struct filename, iname) % sizeof(long) == 0);

static inline struct user_namespace *file_mnt_user_ns(struct file *file)
{
	return mnt_user_ns(file->f_path.mnt);
}

extern long vfs_truncate(const struct path *, loff_t);
int do_truncate(struct user_namespace *, struct dentry *, loff_t start,
		unsigned int time_attrs, struct file *filp);
/* vfs_fallocate, do_sys_open, file_open_name, file_open_root, dentry_open,
   dentry_create, open_with_fake_path removed - unused/internal only */
extern struct file *filp_open(const char *, int, umode_t);
extern int filp_close(struct file *, fl_owner_t id);

extern struct filename *getname_kernel(const char *);
extern void putname(struct filename *name);

extern void __init vfs_caches_init_early(void);
extern void __init vfs_caches_init(void);

extern struct kmem_cache *names_cachep;

#define __getname()		kmem_cache_alloc(names_cachep, GFP_KERNEL)
#define __putname(name)		kmem_cache_free(names_cachep, (void *)(name))

extern const struct file_operations def_chr_fops;

#define CHRDEV_MAJOR_MAX 512

#define CHRDEV_MAJOR_DYN_END 234

#define CHRDEV_MAJOR_DYN_EXT_START 511
#define CHRDEV_MAJOR_DYN_EXT_END 384

extern int alloc_chrdev_region(dev_t *, unsigned, unsigned, const char *);
extern int register_chrdev_region(dev_t, unsigned, const char *);
extern void unregister_chrdev_region(dev_t, unsigned);

extern void init_special_inode(struct inode *, umode_t, dev_t);

static inline ssize_t generic_write_sync(struct kiocb *iocb, ssize_t count)
{
	return count;
}


int notify_change(struct user_namespace *, struct dentry *,
		  struct iattr *, struct inode **);
int inode_permission(struct user_namespace *, struct inode *, int);
int generic_permission(struct user_namespace *, struct inode *, int);

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
static inline void i_readcount_dec(struct inode *inode)
{
}
static inline void i_readcount_inc(struct inode *inode)
{
}
extern ssize_t kernel_read(struct file *, void *, size_t, loff_t *);
ssize_t __kernel_read(struct file *file, void *buf, size_t count, loff_t *pos);
extern ssize_t kernel_write(struct file *, const void *, size_t, loff_t *);
extern ssize_t __kernel_write(struct file *, const void *, size_t, loff_t *);


#include <linux/err.h>


extern int inode_init_always(struct super_block *, struct inode *);
extern unsigned int get_next_ino(void);
extern struct inode *new_inode_pseudo(struct super_block *sb);
extern struct inode *new_inode(struct super_block *sb);
extern int file_remove_privs(struct file *);

static inline void *
alloc_inode_sb(struct super_block *sb, struct kmem_cache *cache, gfp_t gfp)
{
	return kmem_cache_alloc_lru(cache, &sb->s_inode_lru, gfp);
}

extern int generic_file_mmap(struct file *, struct vm_area_struct *);
extern ssize_t generic_write_checks(struct kiocb *, struct iov_iter *);
ssize_t filemap_read(struct kiocb *iocb, struct iov_iter *to,
		ssize_t already_read);
extern ssize_t generic_file_read_iter(struct kiocb *, struct iov_iter *);
extern ssize_t __generic_file_write_iter(struct kiocb *, struct iov_iter *);
extern ssize_t generic_file_write_iter(struct kiocb *, struct iov_iter *);
ssize_t generic_perform_write(struct kiocb *, struct iov_iter *);

extern int nonseekable_open(struct inode * inode, struct file * filp);
/* Removed: stream_open - never called */

#define special_file(m) (S_ISCHR(m)||S_ISBLK(m)||S_ISFIFO(m)||S_ISSOCK(m))

extern struct file_system_type *get_filesystem(struct file_system_type *fs);
extern void put_filesystem(struct file_system_type *fs);
/* Removed: get_super, get_active_super, drop_super, drop_super_exclusive,
   iterate_supers, iterate_supers_type - never called */

extern int simple_setattr(struct user_namespace *, struct dentry *,
			  struct iattr *);
extern int simple_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata);
extern const struct address_space_operations ram_aops;
extern int always_delete_dentry(const struct dentry *);
extern const struct dentry_operations simple_dentry_operations;

extern struct dentry *simple_lookup(struct inode *, struct dentry *, unsigned int flags);



int setattr_prepare(struct user_namespace *, struct dentry *, struct iattr *);
void setattr_copy(struct user_namespace *, struct inode *inode,
		  const struct iattr *attr);

extern int file_update_time(struct file *file);

static inline int iocb_flags(struct file *file)
{
	int res = 0;
	return res;
}





#define __FMODE_EXEC		((__force int) FMODE_EXEC)
#define __FMODE_NONOTIFY	((__force int) FMODE_NONOTIFY)

#define ACC_MODE(x) ("\004\002\006\006"[(x)&O_ACCMODE])
#define OPEN_FMODE(flag) ((__force fmode_t)(((flag + 1) & O_ACCMODE) | 					    (flag & __FMODE_NONOTIFY)))

extern bool path_noexec(const struct path *path);

#endif
