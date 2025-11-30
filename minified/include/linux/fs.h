
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
#include <linux/radix-tree.h>
#include <linux/xarray.h>
#include <linux/rbtree.h>
#include <linux/init.h>
#include <linux/pid.h>
#include <linux/bug.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/mm_types.h>
#include <linux/capability.h>
#include <linux/semaphore.h>
#include <linux/fcntl.h>
#include <linux/rculist_bl.h>
#include <linux/atomic.h>
#include <linux/shrinker.h>
#include <linux/migrate_mode.h>
#include <linux/uidgid.h>
#include <linux/lockdep.h>
#include <linux/percpu-rwsem.h>
#include <linux/workqueue.h>
#include <linux/delayed_call.h>
#include <linux/uuid.h>
#include <linux/errseq.h>
#include <linux/ioprio.h>
#include <linux/fs_types.h>
#include <linux/build_bug.h>
#include <linux/stddef.h>
#include <linux/mount.h>
#include <linux/cred.h>
#include <linux/mnt_idmapping.h>
#include <linux/slab.h>

#include <asm/byteorder.h>
#include <uapi/linux/fs.h>

struct backing_dev_info;
struct bdi_writeback;
struct bio;
struct io_comp_batch;
struct export_operations;
struct fiemap_extent_info;
struct hd_geometry;
struct iovec;
struct kiocb;
struct kobject;
struct pipe_inode_info;
struct poll_table_struct;
struct kstatfs;
struct vm_area_struct;
struct vfsmount;
struct cred;
struct swap_info_struct;
struct seq_file;
struct workqueue_struct;
struct iov_iter;
struct fscrypt_info;
struct fscrypt_operations;
struct fsverity_info;
struct fsverity_operations;
struct fs_context;
struct fs_parameter_spec;
struct fileattr;

extern void __init inode_init(void);
extern void __init inode_init_early(void);
extern void __init files_init(void);
extern void __init files_maxfiles_init(void);
extern unsigned int sysctl_nr_open;

typedef __kernel_rwf_t rwf_t;

struct buffer_head;
typedef int (get_block_t)(struct inode *inode, sector_t iblock,
			struct buffer_head *bh_result, int create);
typedef int (dio_iodone_t)(struct kiocb *iocb, loff_t offset,
			ssize_t bytes, void *private);

#define MAY_EXEC		0x00000001
#define MAY_WRITE		0x00000002
#define MAY_READ		0x00000004
#define MAY_APPEND		0x00000008
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

#define FMODE_NDELAY		((__force fmode_t)0x40)

#define FMODE_EXCL		((__force fmode_t)0x80)

#define FMODE_WRITE_IOCTL	((__force fmode_t)0x100)
/* FMODE_32BITHASH, FMODE_64BITHASH, FMODE_RANDOM removed - unused */
#define FMODE_NOCMTIME		((__force fmode_t)0x800)
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

#define FMODE_BUF_RASYNC	((__force fmode_t)0x40000000)

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
#define ATTR_TOUCH	(1 << 17)

#define WHITEOUT_MODE 0
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

#include <linux/quota.h>

#define FILESYSTEM_MAX_STACK_DEPTH 2

/* Reduced positive_aop_returns - only AOP_TRUNCATED_PAGE used */
enum positive_aop_returns { AOP_TRUNCATED_PAGE = 0x80001 };

struct page;
struct address_space;
struct writeback_control;
struct readahead_control;

enum rw_hint { WRITE_LIFE_NOT_SET = 0 };

#define IOCB_HIPRI		(__force int) RWF_HIPRI
#define IOCB_DSYNC		(__force int) RWF_DSYNC
#define IOCB_SYNC		(__force int) RWF_SYNC
#define IOCB_NOWAIT		(__force int) RWF_NOWAIT
#define IOCB_APPEND		(__force int) RWF_APPEND

#define IOCB_EVENTFD		(1 << 16)
#define IOCB_DIRECT		(1 << 17)
#define IOCB_WRITE		(1 << 18)

#define IOCB_WAITQ		(1 << 19)
#define IOCB_NOIO		(1 << 20)

#define IOCB_ALLOC_CACHE	(1 << 21)

struct kiocb {
	struct file		*ki_filp;

	
	randomized_struct_fields_start

	loff_t			ki_pos;
	void (*ki_complete)(struct kiocb *iocb, long ret);
	void			*private;
	int			ki_flags;
	u16			ki_ioprio; 
	struct wait_page_queue	*ki_waitq; 
	randomized_struct_fields_end
};

static inline bool is_sync_kiocb(struct kiocb *kiocb)
{
	return kiocb->ki_complete == NULL;
}

struct address_space_operations {
	int (*writepage)(struct page *page, struct writeback_control *wbc);
	int (*read_folio)(struct file *, struct folio *);

	
	int (*writepages)(struct address_space *, struct writeback_control *);

	
	bool (*dirty_folio)(struct address_space *, struct folio *);

	void (*readahead)(struct readahead_control *);

	int (*write_begin)(struct file *, struct address_space *mapping,
				loff_t pos, unsigned len,
				struct page **pagep, void **fsdata);
	int (*write_end)(struct file *, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned copied,
				struct page *page, void *fsdata);

	
	sector_t (*bmap)(struct address_space *, sector_t);
	void (*invalidate_folio) (struct folio *, size_t offset, size_t len);
	bool (*release_folio)(struct folio *, gfp_t);
	void (*free_folio)(struct folio *folio);
	ssize_t (*direct_IO)(struct kiocb *, struct iov_iter *iter);
	
	int (*migratepage) (struct address_space *,
			struct page *, struct page *, enum migrate_mode);
	bool (*isolate_page)(struct page *, isolate_mode_t);
	void (*putback_page)(struct page *);
	int (*launder_folio)(struct folio *);
	bool (*is_partially_uptodate) (struct folio *, size_t from,
			size_t count);
	void (*is_dirty_writeback) (struct folio *, bool *dirty, bool *wb);
	int (*error_remove_page)(struct address_space *, struct page *);

	
	int (*swap_activate)(struct swap_info_struct *sis, struct file *file,
				sector_t *span);
	void (*swap_deactivate)(struct file *file);
	int (*swap_rw)(struct kiocb *iocb, struct iov_iter *iter);
};

extern const struct address_space_operations empty_aops;

struct address_space {
	struct inode		*host;
	struct xarray		i_pages;
	struct rw_semaphore	invalidate_lock;
	gfp_t			gfp_mask;
	atomic_t		i_mmap_writable;
	struct rb_root_cached	i_mmap;
	struct rw_semaphore	i_mmap_rwsem;
	unsigned long		nrpages;
	pgoff_t			writeback_index;
	const struct address_space_operations *a_ops;
	unsigned long		flags;
	errseq_t		wb_err;
	spinlock_t		private_lock;
	struct list_head	private_list;
	void			*private_data;
} __attribute__((aligned(sizeof(long)))) __randomize_layout;
	

#define PAGECACHE_TAG_DIRTY	XA_MARK_0
#define PAGECACHE_TAG_WRITEBACK	XA_MARK_1
#define PAGECACHE_TAG_TOWRITE	XA_MARK_2

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

static inline int i_mmap_trylock_read(struct address_space *mapping)
{
	return down_read_trylock(&mapping->i_mmap_rwsem);
}

static inline void i_mmap_lock_read(struct address_space *mapping)
{
	down_read(&mapping->i_mmap_rwsem);
}

static inline void i_mmap_unlock_read(struct address_space *mapping)
{
	up_read(&mapping->i_mmap_rwsem);
}

static inline int mapping_writably_mapped(struct address_space *mapping)
{
	return atomic_read(&mapping->i_mmap_writable) > 0;
}

static inline void mapping_unmap_writable(struct address_space *mapping)
{
	atomic_dec(&mapping->i_mmap_writable);
}

static inline void mapping_allow_writable(struct address_space *mapping)
{
	atomic_inc(&mapping->i_mmap_writable);
}

#define i_size_ordered_init(inode) do { } while (0)

struct posix_acl;
#define ACL_NOT_CACHED ((void *)(-1))

#define ACL_DONT_CACHE ((void *)(-3))

static inline struct posix_acl *
uncached_acl_sentinel(struct task_struct *task)
{
	return (void *)task + 1;
}

static inline bool
is_uncached_acl(struct posix_acl *acl)
{
	return (long)acl & 1;
}

#define IOP_FASTPERM	0x0001
#define IOP_LOOKUP	0x0002
#define IOP_NOFOLLOW	0x0004
#define IOP_XATTR	0x0008
#define IOP_DEFAULT_READLINK	0x0010

struct fsnotify_mark_connector;

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
	struct timespec64	i_ctime;
	spinlock_t		i_lock;	
	unsigned short          i_bytes;
	u8			i_blkbits;
	u8			i_write_hint;
	blkcnt_t		i_blocks;

#ifdef __NEED_I_SIZE_ORDERED
	seqcount_t		i_size_seqcount;
#endif

	
	unsigned long		i_state;
	struct rw_semaphore	i_rwsem;

	unsigned long		dirtied_when;	
	unsigned long		dirtied_time_when;

	struct hlist_node	i_hash;
	struct list_head	i_io_list;	
	struct list_head	i_lru;		
	struct list_head	i_sb_list;
	struct list_head	i_wb_list;	
	union {
		struct hlist_head	i_dentry;
		struct rcu_head		i_rcu;
	};
	atomic64_t		i_version;
	atomic64_t		i_sequence; 
	atomic_t		i_count;
	atomic_t		i_dio_count;
	atomic_t		i_writecount;
	union {
		const struct file_operations	*i_fop;	
		void (*free_inode)(struct inode *);
	};
	struct file_lock_context	*i_flctx;
	struct address_space	i_data;
	struct list_head	i_devices;
	union {
		struct pipe_inode_info	*i_pipe;
		struct cdev		*i_cdev;
		char			*i_link;
		unsigned		i_dir_seq;
	};

	__u32			i_generation;

	void			*i_private; 
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

/* Reduced inode_i_mutex_lock_class - only I_MUTEX_PARENT used */
enum inode_i_mutex_lock_class { I_MUTEX_NORMAL, I_MUTEX_PARENT };

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

static inline int inode_trylock_shared(struct inode *inode)
{
	return down_read_trylock(&inode->i_rwsem);
}

static inline int inode_is_locked(struct inode *inode)
{
	return rwsem_is_locked(&inode->i_rwsem);
}

static inline void inode_lock_nested(struct inode *inode, unsigned subclass)
{
	down_write_nested(&inode->i_rwsem, subclass);
}

static inline void filemap_invalidate_lock(struct address_space *mapping)
{
	down_write(&mapping->invalidate_lock);
}

static inline void filemap_invalidate_unlock(struct address_space *mapping)
{
	up_write(&mapping->invalidate_lock);
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

void lock_two_nondirectories(struct inode *, struct inode*);
void unlock_two_nondirectories(struct inode *, struct inode*);

void filemap_invalidate_lock_two(struct address_space *mapping1,
				 struct address_space *mapping2);
void filemap_invalidate_unlock_two(struct address_space *mapping1,
				   struct address_space *mapping2);

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

static inline unsigned imajor(const struct inode *inode)
{
	return MAJOR(inode->i_rdev);
}

struct fown_struct {
	rwlock_t lock;          
	struct pid *pid;	
	enum pid_type pid_type;	
	kuid_t uid, euid;	
	int signum;		
};

struct file_ra_state {
	pgoff_t start;
	unsigned int size;
	unsigned int async_size;
	unsigned int ra_pages;
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
	struct fown_struct	f_owner;
	const struct cred	*f_cred;
	struct file_ra_state	f_ra;

	u64			f_version;
	
	void			*private_data;

	struct address_space	*f_mapping;
	errseq_t		f_wb_err;
	errseq_t		f_sb_err; 
} __randomize_layout
  __attribute__((aligned(4)));	

struct file_handle;

static inline struct file *get_file(struct file *f)
{
	atomic_long_inc(&f->f_count);
	return f;
}
#define get_file_rcu(x) atomic_long_inc_not_zero(&(x)->f_count)
#define file_count(x)	atomic_long_read(&(x)->f_count)

#define	MAX_NON_LFS	((1UL<<31) - 1)

 
#if BITS_PER_LONG==32
#define MAX_LFS_FILESIZE	((loff_t)ULONG_MAX << PAGE_SHIFT)
#elif BITS_PER_LONG==64
#define MAX_LFS_FILESIZE 	((loff_t)LLONG_MAX)
#endif

#define FL_POSIX	1
#define FL_FLOCK	2
#define FL_DELEG	4	
#define FL_ACCESS	8	
#define FL_EXISTS	16	
#define FL_LEASE	32	
#define FL_CLOSE	64	
#define FL_SLEEP	128	
#define FL_DOWNGRADE_PENDING	256 
#define FL_UNLOCK_PENDING	512 
#define FL_OFDLCK	1024	
#define FL_LAYOUT	2048	
#define FL_RECLAIM	4096	

#define FL_CLOSE_POSIX (FL_POSIX | FL_CLOSE)

#define FILE_LOCK_DEFERRED 1

typedef void *fl_owner_t;

struct file_lock;
struct file_lock_operations;
struct lock_manager_operations;
struct lock_manager;
struct net;
struct file_lock_context;

#ifndef OFFSET_MAX
#define INT_LIMIT(x)	(~((x)1 << (sizeof(x)*8 - 1)))
#define OFFSET_MAX	INT_LIMIT(loff_t)
#define OFFT_OFFSET_MAX	INT_LIMIT(off_t)
#endif


#define locks_inode(f) file_inode(f)

#if BITS_PER_LONG == 32
#endif
static inline void
locks_free_lock_context(struct inode *inode)
{
}

static inline void locks_remove_posix(struct file *filp, fl_owner_t owner)
{
	return;
}

static inline void locks_remove_file(struct file *filp)
{
	return;
}

struct files_struct;
static inline struct inode *file_inode(const struct file *f)
{
	return f->f_inode;
}

static inline struct dentry *file_dentry(const struct file *file)
{
	return d_real(file->f_path.dentry, file_inode(file));
}

struct fasync_struct {
	rwlock_t		fa_lock;
	int			magic;
	int			fa_fd;
	struct fasync_struct	*fa_next; 
	struct file		*fa_file;
	struct rcu_head		fa_rcu;
};

#define FASYNC_MAGIC 0x4601

extern int fasync_helper(int, struct file *, int, struct fasync_struct **);
extern void kill_fasync(struct fasync_struct **, int, int);
extern void __f_setown(struct file *filp, struct pid *, enum pid_type, int force);
extern int f_setown(struct file *filp, unsigned long arg, int force);

#define SB_RDONLY	 1	
#define SB_NOSUID	 2	
#define SB_NODEV	 4	
#define SB_NOEXEC	 8	
#define SB_SYNCHRONOUS	16	
#define SB_MANDLOCK	64	
#define SB_DIRSYNC	128	
#define SB_NOATIME	1024	
#define SB_NODIRATIME	2048	
#define SB_SILENT	32768
#define SB_POSIXACL	(1<<16)	
#define SB_INLINECRYPT	(1<<17)	
#define SB_KERNMOUNT	(1<<22) 
#define SB_I_VERSION	(1<<23) 
#define SB_LAZYTIME	(1<<25) 

#define SB_SUBMOUNT     (1<<26)
#define SB_FORCE    	(1<<27)
#define SB_NOSEC	(1<<28)
#define SB_BORN		(1<<29)
#define SB_ACTIVE	(1<<30)
#define SB_NOUSER	(1<<31)

#define SB_ENC_STRICT_MODE_FL	(1 << 0)

#define sb_has_strict_encoding(sb) \
	(sb->s_encoding_flags & SB_ENC_STRICT_MODE_FL)

#define MNT_FORCE	0x00000001	
#define MNT_DETACH	0x00000002	
#define MNT_EXPIRE	0x00000004	
#define UMOUNT_NOFOLLOW	0x00000008	
#define UMOUNT_UNUSED	0x80000000	

#define SB_I_CGROUPWB	0x00000001	
#define SB_I_NOEXEC	0x00000002	
#define SB_I_NODEV	0x00000004	
#define SB_I_STABLE_WRITES 0x00000008	

#define SB_I_USERNS_VISIBLE		0x00000010 
#define SB_I_IMA_UNVERIFIABLE_SIGNATURE	0x00000020
#define SB_I_UNTRUSTED_MOUNTER		0x00000040

#define SB_I_SKIP_SYNC	0x00000100	
#define SB_I_PERSB_BDI	0x00000200	
#define SB_I_TS_EXPIRY_WARNED 0x00000400 

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
	const struct dquot_operations	*dq_op;
	const struct quotactl_ops	*s_qcop;
	const struct export_operations *s_export_op;
	unsigned long		s_flags;
	unsigned long		s_iflags;	
	unsigned long		s_magic;
	struct dentry		*s_root;
	struct rw_semaphore	s_umount;
	int			s_count;
	atomic_t		s_active;
	const struct xattr_handler **s_xattr;
#if IS_ENABLED(CONFIG_UNICODE)
	struct unicode_map *s_encoding;
	__u16 s_encoding_flags;
#endif
	struct hlist_bl_head	s_roots;	
	struct list_head	s_mounts;	
	struct block_device	*s_bdev;
	struct backing_dev_info *s_bdi;
	struct mtd_info		*s_mtd;
	struct hlist_node	s_instances;
	unsigned int		s_quota_types;	
	struct quota_info	s_dquot;	

	struct sb_writers	s_writers;

	
	void			*s_fs_info;	

	
	u32			s_time_gran;
	
	time64_t		   s_time_min;
	time64_t		   s_time_max;

	char			s_id[32];	
	uuid_t			s_uuid;		

	unsigned int		s_max_links;
	fmode_t			s_mode;

	
	struct mutex s_vfs_rename_mutex;	

	
	const char *s_subtype;

	const struct dentry_operations *s_d_op; 

	struct shrinker s_shrink;	

	
	atomic_long_t s_remove_count;

	
	atomic_long_t s_fsnotify_connectors;

	
	int s_readonly_remount;

	
	errseq_t s_wb_err;

	
	struct workqueue_struct *s_dio_done_wq;
	struct hlist_head s_pins;

	
	struct user_namespace *s_user_ns;

	
	struct list_lru		s_dentry_lru;
	struct list_lru		s_inode_lru;
	struct rcu_head		rcu;
	struct work_struct	destroy_work;

	struct mutex		s_sync_lock;	

	
	int s_stack_depth;

	
	spinlock_t		s_inode_list_lock ____cacheline_aligned_in_smp;
	struct list_head	s_inodes;	

	spinlock_t		s_inode_wblist_lock;
	struct list_head	s_inodes_wb;	
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
	percpu_up_read(sb->s_writers.rw_sem + level-1);
}

static inline void __sb_start_write(struct super_block *sb, int level)
{
	percpu_down_read(sb->s_writers.rw_sem + level - 1);
}

static inline bool __sb_start_write_trylock(struct super_block *sb, int level)
{
	return percpu_down_read_trylock(sb->s_writers.rw_sem + level - 1);
}

#define __sb_writers_acquired(sb, lev)	\
	percpu_rwsem_acquire(&(sb)->s_writers.rw_sem[(lev)-1], 1, _THIS_IP_)
#define __sb_writers_release(sb, lev)	\
	percpu_rwsem_release(&(sb)->s_writers.rw_sem[(lev)-1], 1, _THIS_IP_)

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

static inline bool sb_start_write_trylock(struct super_block *sb)
{
	return __sb_start_write_trylock(sb, SB_FREEZE_WRITE);
}

static inline void sb_start_pagefault(struct super_block *sb)
{
	__sb_start_write(sb, SB_FREEZE_PAGEFAULT);
}

bool inode_owner_or_capable(struct user_namespace *mnt_userns,
			    const struct inode *inode);

int vfs_create(struct user_namespace *, struct inode *,
	       struct dentry *, umode_t, bool);
int vfs_mkdir(struct user_namespace *, struct inode *,
	      struct dentry *, umode_t);
int vfs_mknod(struct user_namespace *, struct inode *, struct dentry *,
              umode_t, dev_t);
int vfs_symlink(struct user_namespace *, struct inode *,
		struct dentry *, const char *);
int vfs_link(struct dentry *, struct user_namespace *, struct inode *,
	     struct dentry *, struct inode **);
int vfs_rmdir(struct user_namespace *, struct inode *, struct dentry *);
int vfs_unlink(struct user_namespace *, struct inode *, struct dentry *,
	       struct inode **);

struct renamedata {
	struct user_namespace *old_mnt_userns;
	struct inode *old_dir;
	struct dentry *old_dentry;
	struct user_namespace *new_mnt_userns;
	struct inode *new_dir;
	struct dentry *new_dentry;
	struct inode **delegated_inode;
	unsigned int flags;
} __randomize_layout;

int vfs_rename(struct renamedata *);

struct dentry *vfs_tmpfile(struct user_namespace *mnt_userns,
			   struct dentry *dentry, umode_t mode, int open_flag);

int vfs_mkobj(struct dentry *, umode_t,
		int (*f)(struct dentry *, umode_t, void *),
		void *);

int vfs_fchown(struct file *file, uid_t user, gid_t group);
int vfs_fchmod(struct file *file, umode_t mode);
int vfs_utimes(const struct path *path, struct timespec64 *times);

extern long vfs_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#define compat_ptr_ioctl NULL

void inode_init_owner(struct user_namespace *mnt_userns, struct inode *inode,
		      const struct inode *dir, umode_t mode);
extern bool may_open_dev(const struct path *path);

struct dir_context;
typedef int (*filldir_t)(struct dir_context *, const char *, int, loff_t, u64,
			 unsigned);

struct dir_context {
	filldir_t actor;
	loff_t pos;
};

#define NOMMU_MAP_COPY		0x00000001
#define NOMMU_MAP_DIRECT	0x00000008
#define NOMMU_MAP_READ		VM_MAYREAD
#define NOMMU_MAP_WRITE		VM_MAYWRITE
#define NOMMU_MAP_EXEC		VM_MAYEXEC

#define NOMMU_VMFLAGS \
	(NOMMU_MAP_READ | NOMMU_MAP_WRITE | NOMMU_MAP_EXEC)

/* REMAP_FILE_* defines removed - unused in minimal kernel */

struct iov_iter;
struct io_uring_cmd;

struct file_operations {
	struct module *owner;
	loff_t (*llseek) (struct file *, loff_t, int);
	ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);
	ssize_t (*write) (struct file *, const char __user *, size_t, loff_t *);
	ssize_t (*read_iter) (struct kiocb *, struct iov_iter *);
	ssize_t (*write_iter) (struct kiocb *, struct iov_iter *);
	int (*iopoll)(struct kiocb *kiocb, struct io_comp_batch *,
			unsigned int flags);
	int (*iterate) (struct file *, struct dir_context *);
	int (*iterate_shared) (struct file *, struct dir_context *);
	__poll_t (*poll) (struct file *, struct poll_table_struct *);
	long (*unlocked_ioctl) (struct file *, unsigned int, unsigned long);
	long (*compat_ioctl) (struct file *, unsigned int, unsigned long);
	int (*mmap) (struct file *, struct vm_area_struct *);
	unsigned long mmap_supported_flags;
	int (*open) (struct inode *, struct file *);
	int (*flush) (struct file *, fl_owner_t id);
	int (*release) (struct inode *, struct file *);
	int (*fsync) (struct file *, loff_t, loff_t, int datasync);
	int (*fasync) (int, struct file *, int);
	int (*lock) (struct file *, int, struct file_lock *);
	ssize_t (*sendpage) (struct file *, struct page *, int, size_t, loff_t *, int);
	unsigned long (*get_unmapped_area)(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);
	int (*check_flags)(int);
	int (*flock) (struct file *, int, struct file_lock *);
	ssize_t (*splice_write)(struct pipe_inode_info *, struct file *, loff_t *, size_t, unsigned int);
	ssize_t (*splice_read)(struct file *, loff_t *, struct pipe_inode_info *, size_t, unsigned int);
	int (*setlease)(struct file *, long, struct file_lock **, void **);
	long (*fallocate)(struct file *file, int mode, loff_t offset,
			  loff_t len);
	void (*show_fdinfo)(struct seq_file *m, struct file *f);
	ssize_t (*copy_file_range)(struct file *, loff_t, struct file *,
			loff_t, size_t, unsigned int);
	loff_t (*remap_file_range)(struct file *file_in, loff_t pos_in,
				   struct file *file_out, loff_t pos_out,
				   loff_t len, unsigned int remap_flags);
	int (*fadvise)(struct file *, loff_t, loff_t, int);
	int (*uring_cmd)(struct io_uring_cmd *ioucmd, unsigned int issue_flags);
} __randomize_layout;

struct inode_operations {
	struct dentry * (*lookup) (struct inode *,struct dentry *, unsigned int);
	const char * (*get_link) (struct dentry *, struct inode *, struct delayed_call *);
	int (*permission) (struct user_namespace *, struct inode *, int);
	struct posix_acl * (*get_acl)(struct inode *, int, bool);

	int (*readlink) (struct dentry *, char __user *,int);

	int (*create) (struct user_namespace *, struct inode *,struct dentry *,
		       umode_t, bool);
	int (*link) (struct dentry *,struct inode *,struct dentry *);
	int (*unlink) (struct inode *,struct dentry *);
	int (*symlink) (struct user_namespace *, struct inode *,struct dentry *,
			const char *);
	int (*mkdir) (struct user_namespace *, struct inode *,struct dentry *,
		      umode_t);
	int (*rmdir) (struct inode *,struct dentry *);
	int (*mknod) (struct user_namespace *, struct inode *,struct dentry *,
		      umode_t,dev_t);
	int (*rename) (struct user_namespace *, struct inode *, struct dentry *,
			struct inode *, struct dentry *, unsigned int);
	int (*setattr) (struct user_namespace *, struct dentry *,
			struct iattr *);
	int (*getattr) (struct user_namespace *, const struct path *,
			struct kstat *, u32, unsigned int);
	ssize_t (*listxattr) (struct dentry *, char *, size_t);
	int (*fiemap)(struct inode *, struct fiemap_extent_info *, u64 start,
		      u64 len);
	int (*update_time)(struct inode *, struct timespec64 *, int);
	int (*atomic_open)(struct inode *, struct dentry *,
			   struct file *, unsigned open_flag,
			   umode_t create_mode);
	int (*tmpfile) (struct user_namespace *, struct inode *,
			struct dentry *, umode_t);
	int (*set_acl)(struct user_namespace *, struct inode *,
		       struct posix_acl *, int);
	int (*fileattr_set)(struct user_namespace *mnt_userns,
			    struct dentry *dentry, struct fileattr *fa);
	int (*fileattr_get)(struct dentry *dentry, struct fileattr *fa);
} ____cacheline_aligned;

static inline ssize_t call_read_iter(struct file *file, struct kiocb *kio,
				     struct iov_iter *iter)
{
	return file->f_op->read_iter(kio, iter);
}

static inline ssize_t call_write_iter(struct file *file, struct kiocb *kio,
				      struct iov_iter *iter)
{
	return file->f_op->write_iter(kio, iter);
}

static inline int call_mmap(struct file *file, struct vm_area_struct *vma)
{
	return file->f_op->mmap(file, vma);
}

extern ssize_t vfs_read(struct file *, char __user *, size_t, loff_t *);
extern ssize_t vfs_write(struct file *, const char __user *, size_t, loff_t *);

struct super_operations {
   	struct inode *(*alloc_inode)(struct super_block *sb);
	void (*destroy_inode)(struct inode *);
	void (*free_inode)(struct inode *);

   	void (*dirty_inode) (struct inode *, int flags);
	int (*write_inode) (struct inode *, struct writeback_control *wbc);
	int (*drop_inode) (struct inode *);
	void (*evict_inode) (struct inode *);
	void (*put_super) (struct super_block *);
	int (*sync_fs)(struct super_block *sb, int wait);
	int (*freeze_super) (struct super_block *);
	int (*freeze_fs) (struct super_block *);
	int (*thaw_super) (struct super_block *);
	int (*unfreeze_fs) (struct super_block *);
	int (*statfs) (struct dentry *, struct kstatfs *);
	int (*remount_fs) (struct super_block *, int *, char *);
	void (*umount_begin) (struct super_block *);

	int (*show_options)(struct seq_file *, struct dentry *);
	int (*show_devname)(struct seq_file *, struct dentry *);
	int (*show_path)(struct seq_file *, struct dentry *);
	int (*show_stats)(struct seq_file *, struct dentry *);
	long (*nr_cached_objects)(struct super_block *,
				  struct shrink_control *);
	long (*free_cached_objects)(struct super_block *,
				    struct shrink_control *);
};

#define S_SYNC		(1 << 0)  
#define S_NOATIME	(1 << 1)  
#define S_APPEND	(1 << 2)  
#define S_IMMUTABLE	(1 << 3)  
#define S_DEAD		(1 << 4)  
#define S_NOQUOTA	(1 << 5)  
#define S_DIRSYNC	(1 << 6)  
#define S_NOCMTIME	(1 << 7)  
#define S_SWAPFILE	(1 << 8)  
#define S_PRIVATE	(1 << 9)  
#define S_IMA		(1 << 10) 
#define S_AUTOMOUNT	(1 << 11) 
#define S_NOSEC		(1 << 12) 
#define S_DAX		0	  
#define S_ENCRYPTED	(1 << 14) 
#define S_CASEFOLD	(1 << 15) 
#define S_VERITY	(1 << 16) 
#define S_KERNEL_FILE	(1 << 17) 

#define __IS_FLG(inode, flg)	((inode)->i_sb->s_flags & (flg))

static inline bool sb_rdonly(const struct super_block *sb) { return sb->s_flags & SB_RDONLY; }
#define IS_RDONLY(inode)	sb_rdonly((inode)->i_sb)
#define IS_SYNC(inode)		(__IS_FLG(inode, SB_SYNCHRONOUS) || \
					((inode)->i_flags & S_SYNC))
#define IS_DIRSYNC(inode)	(__IS_FLG(inode, SB_SYNCHRONOUS|SB_DIRSYNC) || \
					((inode)->i_flags & (S_SYNC|S_DIRSYNC)))
#define IS_MANDLOCK(inode)	__IS_FLG(inode, SB_MANDLOCK)
#define IS_NOATIME(inode)	__IS_FLG(inode, SB_RDONLY|SB_NOATIME)
#define IS_I_VERSION(inode)	__IS_FLG(inode, SB_I_VERSION)

#define IS_NOQUOTA(inode)	((inode)->i_flags & S_NOQUOTA)
#define IS_APPEND(inode)	((inode)->i_flags & S_APPEND)
#define IS_IMMUTABLE(inode)	((inode)->i_flags & S_IMMUTABLE)
#define IS_POSIXACL(inode)	__IS_FLG(inode, SB_POSIXACL)

#define IS_DEADDIR(inode)	((inode)->i_flags & S_DEAD)
#define IS_NOCMTIME(inode)	((inode)->i_flags & S_NOCMTIME)
#define IS_SWAPFILE(inode)	((inode)->i_flags & S_SWAPFILE)
#define IS_PRIVATE(inode)	((inode)->i_flags & S_PRIVATE)
#define IS_IMA(inode)		((inode)->i_flags & S_IMA)
#define IS_AUTOMOUNT(inode)	((inode)->i_flags & S_AUTOMOUNT)
#define IS_NOSEC(inode)		((inode)->i_flags & S_NOSEC)
#define IS_DAX(inode)		((inode)->i_flags & S_DAX)
#define IS_ENCRYPTED(inode)	((inode)->i_flags & S_ENCRYPTED)
#define IS_CASEFOLDED(inode)	((inode)->i_flags & S_CASEFOLD)
#define IS_VERITY(inode)	((inode)->i_flags & S_VERITY)

#define IS_WHITEOUT(inode)	(S_ISCHR(inode->i_mode) && \
				 (inode)->i_rdev == WHITEOUT_DEV)

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
		.ki_ioprio = get_current_ioprio(),
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
#define __I_DIO_WAKEUP		9
#define I_DIO_WAKEUP		(1 << __I_DIO_WAKEUP)
#define I_LINKABLE		(1 << 10)
#define I_DIRTY_TIME		(1 << 11)
#define I_WB_SWITCH		(1 << 13)
#define I_OVL_INUSE		(1 << 14)
#define I_CREATING		(1 << 15)
#define I_DONTCACHE		(1 << 16)
#define I_SYNC_QUEUED		(1 << 17)
#define I_PINNING_FSCACHE_WB	(1 << 18)

#define I_DIRTY_INODE (I_DIRTY_SYNC | I_DIRTY_DATASYNC)
#define I_DIRTY (I_DIRTY_INODE | I_DIRTY_PAGES)
#define I_DIRTY_ALL (I_DIRTY | I_DIRTY_TIME)

extern void __mark_inode_dirty(struct inode *, int);
static inline void mark_inode_dirty(struct inode *inode)
{
	__mark_inode_dirty(inode, I_DIRTY);
}

static inline void mark_inode_dirty_sync(struct inode *inode)
{
	__mark_inode_dirty(inode, I_DIRTY_SYNC);
}

extern void inc_nlink(struct inode *inode);
extern void drop_nlink(struct inode *inode);
extern void clear_nlink(struct inode *inode);
extern void set_nlink(struct inode *inode, unsigned int nlink);

static inline void inode_dec_link_count(struct inode *inode)
{
	drop_nlink(inode);
	mark_inode_dirty(inode);
}

enum file_time_flags {
	S_ATIME = 1,
	S_MTIME = 2,
	S_CTIME = 4,
	S_VERSION = 8,
};

extern bool atime_needs_update(const struct path *, struct inode *);
extern void touch_atime(const struct path *);
int inode_update_time(struct inode *inode, struct timespec64 *time, int flags);

static inline void file_accessed(struct file *file)
{
	if (!(file->f_flags & O_NOATIME))
		touch_atime(&file->f_path);
}

extern int file_modified(struct file *file);

int sync_inode_metadata(struct inode *inode, int wait);

struct file_system_type {
	const char *name;
	int fs_flags;
#define FS_REQUIRES_DEV		1 
#define FS_BINARY_MOUNTDATA	2
#define FS_HAS_SUBTYPE		4
#define FS_USERNS_MOUNT		8	
#define FS_DISALLOW_NOTIFY_PERM	16	
#define FS_ALLOW_IDMAP         32      
#define FS_RENAME_DOES_D_MOVE	32768	
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
	struct lock_class_key i_mutex_dir_key;
};

#define MODULE_ALIAS_FS(NAME) MODULE_ALIAS("fs-" NAME)

extern struct dentry *mount_nodev(struct file_system_type *fs_type,
	int flags, void *data,
	int (*fill_super)(struct super_block *, void *, int));
void generic_shutdown_super(struct super_block *sb);
void kill_block_super(struct super_block *sb);
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
struct super_block *sget(struct file_system_type *type,
			int (*test)(struct super_block *,void *),
			int (*set)(struct super_block *,void *),
			int flags, void *data);

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
extern int unregister_filesystem(struct file_system_type *);
extern int vfs_statfs(const struct path *, struct kstatfs *);

extern int current_umask(void);

extern void ihold(struct inode * inode);
extern void iput(struct inode *);
extern int generic_update_time(struct inode *, struct timespec64 *, int);

extern struct kobject *fs_kobj;

#define MAX_RW_COUNT (INT_MAX & PAGE_MASK)

static inline int break_lease(struct inode *inode, unsigned int mode)
{
	return 0;
}

static inline int try_break_deleg(struct inode *inode, struct inode **delegated_inode)
{
	return 0;
}

static inline int break_deleg_wait(struct inode **delegated_inode)
{
	BUG();
	return 0;
}

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

static inline bool is_idmapped_mnt(const struct vfsmount *mnt)
{
	return mnt_user_ns(mnt) != mnt->mnt_sb->s_user_ns;
}

extern long vfs_truncate(const struct path *, loff_t);
int do_truncate(struct user_namespace *, struct dentry *, loff_t start,
		unsigned int time_attrs, struct file *filp);
extern int vfs_fallocate(struct file *file, int mode, loff_t offset,
			loff_t len);
extern long do_sys_open(int dfd, const char __user *filename, int flags,
			umode_t mode);
extern struct file *file_open_name(struct filename *, int, umode_t);
extern struct file *filp_open(const char *, int, umode_t);
extern struct file *file_open_root(const struct path *,
				   const char *, int, umode_t);
extern struct file * dentry_open(const struct path *, int, const struct cred *);
extern struct file *dentry_create(const struct path *path, int flags,
				  umode_t mode, const struct cred *cred);
extern struct file * open_with_fake_path(const struct path *, int,
					 struct inode*, const struct cred *);
extern int filp_close(struct file *, fl_owner_t id);

extern struct filename *getname_flags(const char __user *, int, int *);
extern struct filename *getname_uflags(const char __user *, int);
extern struct filename *getname(const char __user *);
extern struct filename *getname_kernel(const char *);
extern void putname(struct filename *name);

extern int finish_open(struct file *file, struct dentry *dentry,
			int (*open)(struct inode *, struct file *));
extern int finish_no_open(struct file *file, struct dentry *dentry);

extern void __init vfs_caches_init_early(void);
extern void __init vfs_caches_init(void);

extern struct kmem_cache *names_cachep;

#define __getname()		kmem_cache_alloc(names_cachep, GFP_KERNEL)
#define __putname(name)		kmem_cache_free(names_cachep, (void *)(name))

extern struct super_block *blockdev_superblock;
void emergency_thaw_all(void);
extern int sync_filesystem(struct super_block *);
extern const struct file_operations def_blk_fops;
extern const struct file_operations def_chr_fops;

#define CHRDEV_MAJOR_MAX 512

#define CHRDEV_MAJOR_DYN_END 234

#define CHRDEV_MAJOR_DYN_EXT_START 511
#define CHRDEV_MAJOR_DYN_EXT_END 384

extern int alloc_chrdev_region(dev_t *, unsigned, unsigned, const char *);
extern int register_chrdev_region(dev_t, unsigned, const char *);
extern int __register_chrdev(unsigned int major, unsigned int baseminor,
			     unsigned int count, const char *name,
			     const struct file_operations *fops);
extern void __unregister_chrdev(unsigned int major, unsigned int baseminor,
				unsigned int count, const char *name);
extern void unregister_chrdev_region(dev_t, unsigned);

static inline int register_chrdev(unsigned int major, const char *name,
				  const struct file_operations *fops)
{
	return __register_chrdev(major, 0, 256, name, fops);
}

extern void init_special_inode(struct inode *, umode_t, dev_t);

extern void make_bad_inode(struct inode *);
extern bool is_bad_inode(struct inode *);

extern int __must_check file_fdatawait_range(struct file *file, loff_t lstart,
						loff_t lend);
extern int __must_check file_check_and_advance_wb_err(struct file *file);
extern int __must_check file_write_and_wait_range(struct file *file,
						loff_t start, loff_t end);

extern int vfs_fsync_range(struct file *file, loff_t start, loff_t end,
			   int datasync);
extern int vfs_fsync(struct file *file, int datasync);

extern int sync_file_range(struct file *file, loff_t offset, loff_t nbytes,
				unsigned int flags);

static inline ssize_t generic_write_sync(struct kiocb *iocb, ssize_t count)
{
	if (iocb->ki_flags & IOCB_DSYNC) {
		int ret = vfs_fsync_range(iocb->ki_filp,
				iocb->ki_pos - count, iocb->ki_pos - 1,
				(iocb->ki_flags & IOCB_SYNC) ? 0 : 1);
		if (ret)
			return ret;
	}

	return count;
}

extern void emergency_sync(void);
extern void emergency_remount(void);

int notify_change(struct user_namespace *, struct dentry *,
		  struct iattr *, struct inode **);
int inode_permission(struct user_namespace *, struct inode *, int);
int generic_permission(struct user_namespace *, struct inode *, int);
static inline int file_permission(struct file *file, int mask)
{
	return inode_permission(file_mnt_user_ns(file),
				file_inode(file), mask);
}
static inline int path_permission(const struct path *path, int mask)
{
	return inode_permission(mnt_user_ns(path->mnt),
				d_inode(path->dentry), mask);
}
int __check_sticky(struct user_namespace *mnt_userns, struct inode *dir,
		   struct inode *inode);

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
	return;
}
static inline void i_readcount_inc(struct inode *inode)
{
	return;
}
extern ssize_t kernel_read(struct file *, void *, size_t, loff_t *);
ssize_t __kernel_read(struct file *file, void *buf, size_t count, loff_t *pos);
extern ssize_t kernel_write(struct file *, const void *, size_t, loff_t *);
extern ssize_t __kernel_write(struct file *, const void *, size_t, loff_t *);
extern struct file * open_exec(const char *);
 

extern bool is_subdir(struct dentry *, struct dentry *);
extern bool path_is_under(const struct path *, const struct path *);

extern char *file_path(struct file *, char *, int);

#include <linux/err.h>

extern loff_t default_llseek(struct file *file, loff_t offset, int whence);

extern loff_t vfs_llseek(struct file *file, loff_t offset, int whence);

extern int inode_init_always(struct super_block *, struct inode *);
extern void inode_init_once(struct inode *);
extern void address_space_init_once(struct address_space *mapping);
extern struct inode * igrab(struct inode *);
extern ino_t iunique(struct super_block *, ino_t);
extern int inode_needs_sync(struct inode *inode);
extern int generic_delete_inode(struct inode *inode);
static inline int generic_drop_inode(struct inode *inode)
{
	return !inode->i_nlink || inode_unhashed(inode);
}
extern void d_mark_dontcache(struct inode *inode);

extern struct inode *ilookup5_nowait(struct super_block *sb,
		unsigned long hashval, int (*test)(struct inode *, void *),
		void *data);
extern struct inode *ilookup5(struct super_block *sb, unsigned long hashval,
		int (*test)(struct inode *, void *), void *data);
extern struct inode *ilookup(struct super_block *sb, unsigned long ino);

extern struct inode *inode_insert5(struct inode *inode, unsigned long hashval,
		int (*test)(struct inode *, void *),
		int (*set)(struct inode *, void *),
		void *data);
extern struct inode * iget5_locked(struct super_block *, unsigned long, int (*test)(struct inode *, void *), int (*set)(struct inode *, void *), void *);
extern struct inode * iget_locked(struct super_block *, unsigned long);
extern struct inode *find_inode_nowait(struct super_block *,
				       unsigned long,
				       int (*match)(struct inode *,
						    unsigned long, void *),
				       void *data);
extern struct inode *find_inode_rcu(struct super_block *, unsigned long,
				    int (*)(struct inode *, void *), void *);
extern struct inode *find_inode_by_ino_rcu(struct super_block *, unsigned long);
extern int insert_inode_locked4(struct inode *, unsigned long, int (*test)(struct inode *, void *), void *);
extern int insert_inode_locked(struct inode *);
static inline void lockdep_annotate_inode_mutex_key(struct inode *inode) { };
extern void unlock_new_inode(struct inode *);
extern void discard_new_inode(struct inode *);
extern unsigned int get_next_ino(void);
extern void evict_inodes(struct super_block *sb);
void dump_mapping(const struct address_space *);

extern void __iget(struct inode * inode);
extern void iget_failed(struct inode *);
extern void clear_inode(struct inode *);
extern void __destroy_inode(struct inode *);
extern struct inode *new_inode_pseudo(struct super_block *sb);
extern struct inode *new_inode(struct super_block *sb);
extern void free_inode_nonrcu(struct inode *inode);
extern int should_remove_suid(struct dentry *);
extern int file_remove_privs(struct file *);

static inline void *
alloc_inode_sb(struct super_block *sb, struct kmem_cache *cache, gfp_t gfp)
{
	return kmem_cache_alloc_lru(cache, &sb->s_inode_lru, gfp);
}

extern void __insert_inode_hash(struct inode *, unsigned long hashval);
extern void __remove_inode_hash(struct inode *);
static inline void remove_inode_hash(struct inode *inode)
{
	if (!inode_unhashed(inode) && !hlist_fake(&inode->i_hash))
		__remove_inode_hash(inode);
}

extern void inode_sb_list_add(struct inode *inode);
extern void inode_add_lru(struct inode *inode);

extern int generic_file_mmap(struct file *, struct vm_area_struct *);
extern int generic_file_readonly_mmap(struct file *, struct vm_area_struct *);
extern ssize_t generic_write_checks(struct kiocb *, struct iov_iter *);
int generic_write_checks_count(struct kiocb *iocb, loff_t *count);
extern int generic_write_check_limits(struct file *file, loff_t pos,
		loff_t *count);
extern int generic_file_rw_checks(struct file *file_in, struct file *file_out);
ssize_t filemap_read(struct kiocb *iocb, struct iov_iter *to,
		ssize_t already_read);
extern ssize_t generic_file_read_iter(struct kiocb *, struct iov_iter *);
extern ssize_t __generic_file_write_iter(struct kiocb *, struct iov_iter *);
extern ssize_t generic_file_write_iter(struct kiocb *, struct iov_iter *);
extern ssize_t generic_file_direct_write(struct kiocb *, struct iov_iter *);
ssize_t generic_perform_write(struct kiocb *, struct iov_iter *);

ssize_t vfs_iter_read(struct file *file, struct iov_iter *iter, loff_t *ppos,
		rwf_t flags);
ssize_t vfs_iter_write(struct file *file, struct iov_iter *iter, loff_t *ppos,
		rwf_t flags);
ssize_t vfs_iocb_iter_read(struct file *file, struct kiocb *iocb,
			   struct iov_iter *iter);
ssize_t vfs_iocb_iter_write(struct file *file, struct kiocb *iocb,
			    struct iov_iter *iter);

extern ssize_t generic_file_splice_read(struct file *, loff_t *,
		struct pipe_inode_info *, size_t, unsigned int);
extern ssize_t iter_file_splice_write(struct pipe_inode_info *,
		struct file *, loff_t *, size_t, unsigned int);
extern ssize_t generic_splice_sendpage(struct pipe_inode_info *pipe,
		struct file *out, loff_t *, size_t len, unsigned int flags);
extern long do_splice_direct(struct file *in, loff_t *ppos, struct file *out,
		loff_t *opos, size_t len, unsigned int flags);

extern void
file_ra_state_init(struct file_ra_state *ra, struct address_space *mapping);
extern loff_t noop_llseek(struct file *file, loff_t offset, int whence);
extern loff_t no_llseek(struct file *file, loff_t offset, int whence);
extern loff_t vfs_setpos(struct file *file, loff_t offset, loff_t maxsize);
extern loff_t generic_file_llseek(struct file *file, loff_t offset, int whence);
extern loff_t generic_file_llseek_size(struct file *file, loff_t offset,
		int whence, loff_t maxsize, loff_t eof);
extern loff_t fixed_size_llseek(struct file *file, loff_t offset,
		int whence, loff_t size);
extern loff_t no_seek_end_llseek_size(struct file *, loff_t, int, loff_t);
extern loff_t no_seek_end_llseek(struct file *, loff_t, int);
int rw_verify_area(int, struct file *, const loff_t *, size_t);
extern int generic_file_open(struct inode * inode, struct file * filp);
extern int nonseekable_open(struct inode * inode, struct file * filp);
extern int stream_open(struct inode * inode, struct file * filp);

void inode_dio_wait(struct inode *inode);

void dio_warn_stale_pagecache(struct file *filp);

extern void inode_set_flags(struct inode *inode, unsigned int flags,
			    unsigned int mask);

extern const struct file_operations generic_ro_fops;

#define special_file(m) (S_ISCHR(m)||S_ISBLK(m)||S_ISFIFO(m)||S_ISSOCK(m))

extern int readlink_copy(char __user *, int, const char *);
extern int page_readlink(struct dentry *, char __user *, int);
extern const char *page_get_link(struct dentry *, struct inode *,
				 struct delayed_call *);
extern void page_put_link(void *);
extern int page_symlink(struct inode *inode, const char *symname, int len);
extern const struct inode_operations page_symlink_inode_operations;
extern void kfree_link(void *);
void generic_fillattr(struct user_namespace *, struct inode *, struct kstat *);
void generic_fill_statx_attr(struct inode *inode, struct kstat *stat);
extern int vfs_getattr_nosec(const struct path *, struct kstat *, u32, unsigned int);
extern int vfs_getattr(const struct path *, struct kstat *, u32, unsigned int);
void __inode_add_bytes(struct inode *inode, loff_t bytes);
void inode_add_bytes(struct inode *inode, loff_t bytes);
void __inode_sub_bytes(struct inode *inode, loff_t bytes);
void inode_sub_bytes(struct inode *inode, loff_t bytes);
loff_t inode_get_bytes(struct inode *inode);
void inode_set_bytes(struct inode *inode, loff_t bytes);
const char *simple_get_link(struct dentry *, struct inode *,
			    struct delayed_call *);
extern const struct inode_operations simple_symlink_inode_operations;

int vfs_fstatat(int dfd, const char __user *filename, struct kstat *stat,
		int flags);
int vfs_fstat(int fd, struct kstat *stat);

extern const char *vfs_get_link(struct dentry *, struct delayed_call *);
extern int vfs_readlink(struct dentry *, char __user *, int);

extern struct file_system_type *get_filesystem(struct file_system_type *fs);
extern void put_filesystem(struct file_system_type *fs);
extern struct file_system_type *get_fs_type(const char *name);
extern struct super_block *get_super(struct block_device *);
extern struct super_block *get_active_super(struct block_device *bdev);
extern void drop_super(struct super_block *sb);
extern void drop_super_exclusive(struct super_block *sb);
extern void iterate_supers(void (*)(struct super_block *, void *), void *);
extern void iterate_supers_type(struct file_system_type *,
			        void (*)(struct super_block *, void *), void *);

extern int dcache_dir_open(struct inode *, struct file *);
extern int dcache_dir_close(struct inode *, struct file *);
extern loff_t dcache_dir_lseek(struct file *, loff_t, int);
extern int dcache_readdir(struct file *, struct dir_context *);
extern int simple_setattr(struct user_namespace *, struct dentry *,
			  struct iattr *);
extern int simple_getattr(struct user_namespace *, const struct path *,
			  struct kstat *, u32, unsigned int);
extern int simple_statfs(struct dentry *, struct kstatfs *);
extern int simple_open(struct inode *inode, struct file *file);
extern int simple_link(struct dentry *, struct inode *, struct dentry *);
extern int simple_unlink(struct inode *, struct dentry *);
extern int simple_rmdir(struct inode *, struct dentry *);
extern int simple_rename_exchange(struct inode *old_dir, struct dentry *old_dentry,
				  struct inode *new_dir, struct dentry *new_dentry);
extern int simple_rename(struct user_namespace *, struct inode *,
			 struct dentry *, struct inode *, struct dentry *,
			 unsigned int);
extern void simple_recursive_removal(struct dentry *,
                              void (*callback)(struct dentry *));
extern int noop_fsync(struct file *, loff_t, loff_t, int);
extern ssize_t noop_direct_IO(struct kiocb *iocb, struct iov_iter *iter);
extern int simple_empty(struct dentry *);
extern int simple_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata);
extern const struct address_space_operations ram_aops;
extern int always_delete_dentry(const struct dentry *);
extern struct inode *alloc_anon_inode(struct super_block *);
extern const struct dentry_operations simple_dentry_operations;

extern struct dentry *simple_lookup(struct inode *, struct dentry *, unsigned int flags);
extern ssize_t generic_read_dir(struct file *, char __user *, size_t, loff_t *);
extern const struct file_operations simple_dir_operations;
extern const struct inode_operations simple_dir_inode_operations;
extern void make_empty_dir_inode(struct inode *inode);
extern bool is_empty_dir_inode(struct inode *inode);
struct tree_descr { const char *name; const struct file_operations *ops; int mode; };
struct dentry *d_alloc_name(struct dentry *, const char *);
extern int simple_pin_fs(struct file_system_type *, struct vfsmount **mount, int *count);
extern void simple_release_fs(struct vfsmount **mount, int *count);


#define buffer_migrate_page NULL
#define buffer_migrate_page_norefs NULL

int may_setattr(struct user_namespace *mnt_userns, struct inode *inode,
		unsigned int ia_valid);
int setattr_prepare(struct user_namespace *, struct dentry *, struct iattr *);
extern int inode_newsize_ok(const struct inode *, loff_t offset);
void setattr_copy(struct user_namespace *, struct inode *inode,
		  const struct iattr *attr);

extern int file_update_time(struct file *file);

static inline bool vma_is_dax(const struct vm_area_struct *vma)
{
	return vma->vm_file && IS_DAX(vma->vm_file->f_mapping->host);
}

static inline bool vma_is_fsdax(struct vm_area_struct *vma)
{
	struct inode *inode;

	if (!IS_ENABLED(CONFIG_FS_DAX) || !vma->vm_file)
		return false;
	if (!vma_is_dax(vma))
		return false;
	inode = file_inode(vma->vm_file);
	if (S_ISCHR(inode->i_mode))
		return false; 
	return true;
}

static inline int iocb_flags(struct file *file)
{
	int res = 0;
	if (file->f_flags & O_APPEND)
		res |= IOCB_APPEND;
	if (file->f_flags & O_DIRECT)
		res |= IOCB_DIRECT;
	if ((file->f_flags & O_DSYNC) || IS_SYNC(file->f_mapping->host))
		res |= IOCB_DSYNC;
	if (file->f_flags & __O_SYNC)
		res |= IOCB_SYNC;
	return res;
}

static inline int kiocb_set_rw_flags(struct kiocb *ki, rwf_t flags)
{
	int kiocb_flags = 0;

	
	BUILD_BUG_ON((__force int) RWF_SUPPORTED & IOCB_EVENTFD);

	if (!flags)
		return 0;
	if (unlikely(flags & ~RWF_SUPPORTED))
		return -EOPNOTSUPP;

	if (flags & RWF_NOWAIT) {
		if (!(ki->ki_filp->f_mode & FMODE_NOWAIT))
			return -EOPNOTSUPP;
		kiocb_flags |= IOCB_NOIO;
	}
	kiocb_flags |= (__force int) (flags & RWF_SUPPORTED);
	if (flags & RWF_SYNC)
		kiocb_flags |= IOCB_DSYNC;

	ki->ki_flags |= kiocb_flags;
	return 0;
}

static inline ino_t parent_ino(struct dentry *dentry)
{
	ino_t res;

	
	spin_lock(&dentry->d_lock);
	res = dentry->d_parent->d_inode->i_ino;
	spin_unlock(&dentry->d_lock);
	return res;
}

struct simple_transaction_argresp {
	ssize_t size;
	char data[];
};

#define SIMPLE_TRANSACTION_LIMIT (PAGE_SIZE - sizeof(struct simple_transaction_argresp))

char *simple_transaction_get(struct file *file, const char __user *buf,
				size_t size);
ssize_t simple_transaction_read(struct file *file, char __user *buf,
				size_t size, loff_t *pos);
int simple_transaction_release(struct inode *inode, struct file *file);

void simple_transaction_set(struct file *file, size_t n);

#define DEFINE_SIMPLE_ATTRIBUTE(__fops, __get, __set, __fmt)		\
static int __fops ## _open(struct inode *inode, struct file *file)	\
{									\
	__simple_attr_check_format(__fmt, 0ull);			\
	return simple_attr_open(inode, file, __get, __set, __fmt);	\
}									\
static const struct file_operations __fops = {				\
	.owner	 = THIS_MODULE,						\
	.open	 = __fops ## _open,					\
	.release = simple_attr_release,					\
	.read	 = simple_attr_read,					\
	.write	 = simple_attr_write,					\
	.llseek	 = generic_file_llseek,					\
}

static inline __printf(1, 2)
void __simple_attr_check_format(const char *fmt, ...)
{
	
}

int simple_attr_open(struct inode *inode, struct file *file,
		     int (*get)(void *, u64 *), int (*set)(void *, u64),
		     const char *fmt);
int simple_attr_release(struct inode *inode, struct file *file);
ssize_t simple_attr_read(struct file *file, char __user *buf,
			 size_t len, loff_t *ppos);
ssize_t simple_attr_write(struct file *file, const char __user *buf,
			  size_t len, loff_t *ppos);

struct ctl_table;
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

static inline int check_sticky(struct user_namespace *mnt_userns,
			       struct inode *dir, struct inode *inode)
{
	if (!(dir->i_mode & S_ISVTX))
		return 0;

	return __check_sticky(mnt_userns, dir, inode);
}

static inline void inode_has_no_xattr(struct inode *inode)
{
	if (!is_sxid(inode->i_mode) && (inode->i_sb->s_flags & SB_NOSEC))
		inode->i_flags |= S_NOSEC;
}

static inline bool dir_emit(struct dir_context *ctx,
			    const char *name, int namelen,
			    u64 ino, unsigned type)
{
	return ctx->actor(ctx, name, namelen, ctx->pos, ino, type) == 0;
}
static inline bool dir_emit_dot(struct file *file, struct dir_context *ctx)
{
	return ctx->actor(ctx, ".", 1, ctx->pos,
			  file->f_path.dentry->d_inode->i_ino, DT_DIR) == 0;
}
static inline bool dir_emit_dotdot(struct file *file, struct dir_context *ctx)
{
	return ctx->actor(ctx, "..", 2, ctx->pos,
			  parent_ino(file->f_path.dentry), DT_DIR) == 0;
}
static inline bool dir_emit_dots(struct file *file, struct dir_context *ctx)
{
	if (ctx->pos == 0) {
		if (!dir_emit_dot(file, ctx))
			return false;
		ctx->pos = 1;
	}
	if (ctx->pos == 1) {
		if (!dir_emit_dotdot(file, ctx))
			return false;
		ctx->pos = 2;
	}
	return true;
}
extern bool path_noexec(const struct path *path);
extern void inode_nohighmem(struct inode *inode);

#endif
