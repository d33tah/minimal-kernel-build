
#ifndef _LINUX_FS_H
#define _LINUX_FS_H

#include <linux/linkage.h>
#include <linux/wait_bit.h>
#include <linux/kdev_t.h>
/* dcache.h inlined - single includer */
#include <linux/atomic.h>
#include <linux/math.h>
#include <linux/rculist.h>
#include <linux/rculist_bl.h>
#include <linux/spinlock.h>
#include <linux/seqlock.h>
#include <linux/rcupdate.h>
#include <generated/bounds.h>
struct lockref { spinlock_t lock; int count; };
extern void lockref_get(struct lockref *);
extern int lockref_put_return(struct lockref *);
extern void lockref_mark_dead(struct lockref *);
extern int lockref_get_not_dead(struct lockref *);
#include <linux/hash.h>

#define hashlen_hash(hashlen) ((u32)(hashlen))
#define hashlen_len(hashlen)  ((u32)((hashlen) >> 32))
#define hashlen_create(hash, len) ((u64)(len)<<32 | (u32)(hash))

#include <linux/wait.h>

#define IS_ROOT(x) ((x) == (x)->d_parent)

#define HASH_LEN_DECLARE u32 hash; u32 len
#define bytemask_from_count(cnt)	(~(~0ul << (cnt)*8))

struct qstr {
	union {
		struct {
			HASH_LEN_DECLARE;
		};
		u64 hash_len;
	};
	const unsigned char *name;
};

#define QSTR_INIT(n,l) { { { .len = l } }, .name = n }

#  define DNAME_INLINE_LEN 40

#define d_lock	d_lockref.lock

struct dentry {

	unsigned int d_flags;
	seqcount_spinlock_t d_seq;
	struct hlist_bl_node d_hash;
	struct dentry *d_parent;
	struct qstr d_name;
	struct inode *d_inode;
	unsigned char d_iname[DNAME_INLINE_LEN];

	struct lockref d_lockref;
	struct super_block *d_sb;

	union {
		struct list_head d_lru;
		wait_queue_head_t *d_wait;
	};
	struct list_head d_child;
	struct list_head d_subdirs;

	union {
		struct hlist_node d_alias;
		struct hlist_bl_node d_in_lookup_hash;
	 	struct rcu_head d_rcu;
	} d_u;
} __randomize_layout;

enum dentry_d_lock_class
{
	DENTRY_D_LOCK_NORMAL,
	DENTRY_D_LOCK_NESTED
};

#define DCACHE_CANT_MOUNT		0x00000100
#define DCACHE_SHRINK_LIST		0x00000400

#define DCACHE_DENTRY_KILLED		0x00008000

#define DCACHE_MOUNTED			0x00010000
#define DCACHE_NEED_AUTOMOUNT		0x00020000
#define DCACHE_MANAGED_DENTRY \
	(DCACHE_MOUNTED|DCACHE_NEED_AUTOMOUNT|0x00040000)

#define DCACHE_LRU_LIST			0x00080000

#define DCACHE_ENTRY_TYPE		0x00700000
#define DCACHE_MISS_TYPE		0x00000000
#define DCACHE_DIRECTORY_TYPE		0x00200000
#define DCACHE_AUTODIR_TYPE		0x00300000
#define DCACHE_REGULAR_TYPE		0x00400000
#define DCACHE_SPECIAL_TYPE		0x00500000

#define DCACHE_MAY_FREE			0x00800000
#define DCACHE_FALLTHRU			0x01000000

#define DCACHE_PAR_LOOKUP		0x10000000
#define DCACHE_NORCU			0x40000000

extern void d_instantiate(struct dentry *, struct inode *);

extern struct dentry * d_alloc_parallel(struct dentry *, const struct qstr *,
					wait_queue_head_t *);
extern void shrink_dcache_for_umount(struct super_block *);

extern struct dentry * d_make_root(struct inode *);

extern void d_add(struct dentry *, struct inode *);
extern struct dentry *d_lookup(const struct dentry *, const struct qstr *);
extern struct dentry *__d_lookup(const struct dentry *, const struct qstr *);
extern struct dentry *__d_lookup_rcu(const struct dentry *parent,
				const struct qstr *name, unsigned *seq);

static inline struct dentry *dget(struct dentry *dentry)
{
	if (dentry)
		lockref_get(&dentry->d_lockref);
	return dentry;
}

static inline int d_unhashed(const struct dentry *dentry)
{
	return hlist_bl_unhashed(&dentry->d_hash);
}

static inline int d_unlinked(const struct dentry *dentry)
{
	return d_unhashed(dentry) && !IS_ROOT(dentry);
}

extern void __d_lookup_done(struct dentry *);

static inline int d_in_lookup(const struct dentry *dentry)
{
	return dentry->d_flags & DCACHE_PAR_LOOKUP;
}

static inline void d_lookup_done(struct dentry *dentry)
{
	if (unlikely(d_in_lookup(dentry))) {
		spin_lock(&dentry->d_lock);
		__d_lookup_done(dentry);
		spin_unlock(&dentry->d_lock);
	}
}

extern void dput(struct dentry *);

static inline bool d_mountpoint(const struct dentry *dentry)
{
	return dentry->d_flags & DCACHE_MOUNTED;
}

static inline unsigned __d_entry_type(const struct dentry *dentry) {
  return dentry->d_flags & DCACHE_ENTRY_TYPE;
}

static inline bool d_can_lookup(const struct dentry *dentry) {
  return __d_entry_type(dentry) == DCACHE_DIRECTORY_TYPE;
}

extern int sysctl_vfs_cache_pressure;

static inline struct inode *d_inode(const struct dentry *dentry)
{
	return dentry->d_inode;
}
/* end dcache.h inline */
#ifndef _LINUX_PATH_H
#define _LINUX_PATH_H
struct dentry;
struct vfsmount;
struct path { struct vfsmount *mnt; struct dentry *dentry; } __randomize_layout;
extern void path_get(const struct path *);
extern void path_put(const struct path *);
#endif
#include <linux/stat.h>
#include <linux/cache.h>
#include <linux/list.h>
#include <linux/list_lru.h>
/* llist.h inlined */
struct llist_head { struct llist_node *first; };
struct llist_node { struct llist_node *next; };
#define LLIST_HEAD_INIT(name)	{ NULL }
#define LLIST_HEAD(name)	struct llist_head name = LLIST_HEAD_INIT(name)
#define llist_entry(ptr, type, member) container_of(ptr, type, member)
#define member_address_is_nonnull(ptr, member) ((uintptr_t)(ptr) + offsetof(typeof(*(ptr)), member) != 0)
#define llist_for_each_entry_safe(pos, n, node, member) for (pos = llist_entry((node), typeof(*pos), member); member_address_is_nonnull(pos, member) && (n = llist_entry(pos->member.next, typeof(*n), member), true); pos = n)
extern bool llist_add_batch(struct llist_node *new_first, struct llist_node *new_last, struct llist_head *head);
static inline bool llist_add(struct llist_node *new, struct llist_head *head) { return llist_add_batch(new, new, head); }
static inline struct llist_node *llist_del_all(struct llist_head *head) { return xchg(&head->first, NULL); }
#include <linux/xarray.h>
#include <linux/rbtree.h>
#include <linux/init.h>
#include <linux/pid.h>
#include <linux/bug.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/mm_types.h>
struct semaphore {
	raw_spinlock_t		lock;
	unsigned int		count;
	struct list_head	wait_list;
};
#define __SEMAPHORE_INITIALIZER(name, n)				\
{									\
	.lock		= __RAW_SPIN_LOCK_UNLOCKED((name).lock),	\
	.count		= n,						\
	.wait_list	= LIST_HEAD_INIT((name).wait_list),		\
}
#define DEFINE_SEMAPHORE(name)	\
	struct semaphore name = __SEMAPHORE_INITIALIZER(name, 1)
extern void down(struct semaphore *sem);
extern int __must_check down_trylock(struct semaphore *sem);
extern void up(struct semaphore *sem);
#include <linux/fcntl.h>
#include <linux/atomic.h>
/* shrinker types defined in list_lru.h */
#include <linux/list_lru.h>
#include <linux/uidgid.h>
#include <linux/lockdep.h>
#include <linux/percpu-rwsem.h>
#include <linux/workqueue.h>

struct delayed_call {
	void (*fn)(void *);
	void *arg;
};
static inline void do_delayed_call(struct delayed_call *call)
{
	if (call->fn)
		call->fn(call->arg);
}

#include <linux/build_bug.h>

#include <linux/stddef.h>
#include <linux/mount.h>
#include <linux/cred.h>
#include <linux/mnt_idmapping.h>
#include <linux/slab.h>

#include <linux/limits.h>
#define INR_OPEN_CUR 1024
#define INR_OPEN_MAX 4096
/* end uapi/linux/fs.h */

/* Unused forward decls removed: bdi_writeback, bio, iovec, kobject,
   poll_table_struct, kstatfs, vfsmount, seq_file, workqueue_struct, pipe_inode_info */
struct backing_dev_info;
struct kiocb;
struct vm_area_struct;
struct cred;
struct iov_iter;
struct fs_context;
struct fs_parameter_spec;

extern void __init inode_init(void);
extern void __init inode_init_early(void);
extern void __init files_init(void);

#define MAY_EXEC		0x00000001
#define MAY_ACCESS		0x00000010
#define MAY_CHDIR		0x00000040

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
#define FMODE_NONOTIFY		((__force fmode_t)0x4000000)
#define FMODE_NEED_UNMOUNT	((__force fmode_t)0x10000000)

/* Reduced positive_aop_returns - only AOP_TRUNCATED_PAGE used */
enum positive_aop_returns { AOP_TRUNCATED_PAGE = 0x80001 };

struct page;
struct address_space;

#define IOCB_DIRECT		(1 << 17)

struct kiocb {
	struct file		*ki_filp;

	randomized_struct_fields_start

	loff_t			ki_pos;
	int			ki_flags;
	randomized_struct_fields_end
};

struct address_space_operations {
	int (*read_folio)(struct file *, struct folio *);
	int (*write_begin)(struct file *, struct address_space *mapping, loff_t pos,
			   unsigned len, struct page **pagep, void **fsdata);
	int (*write_end)(struct file *, struct address_space *mapping, loff_t pos,
			 unsigned len, unsigned copied, struct page *page, void *fsdata);
};

struct address_space {
	struct inode		*host;
	struct xarray		i_pages;
	struct rw_semaphore	invalidate_lock;
	gfp_t			gfp_mask;
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

#define IOP_LOOKUP	0x0002
#define IOP_NOFOLLOW	0x0004

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
	spinlock_t		i_lock;

	unsigned long		i_state;
	struct rw_semaphore	i_rwsem;

	struct list_head	i_lru;
	struct list_head	i_sb_list;
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

static inline void filemap_invalidate_lock_shared(struct address_space *mapping)
{
	down_read(&mapping->invalidate_lock);
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
	struct address_space	*f_mapping;
} __randomize_layout
  __attribute__((aligned(4)));

static inline struct file *get_file(struct file *f)
{
	atomic_long_inc(&f->f_count);
	return f;
}
#define file_count(x)	atomic_long_read(&(x)->f_count)

#define	MAX_NON_LFS	((1UL<<31) - 1)

/* 32-bit only kernel */
#define MAX_LFS_FILESIZE	((loff_t)ULONG_MAX << PAGE_SHIFT)

typedef void *fl_owner_t;

static inline struct inode *file_inode(const struct file *f)
{
	return f->f_inode;
}

#define SB_RDONLY	 1
#define SB_SYNCHRONOUS	16
#define SB_MANDLOCK	64
#define SB_DIRSYNC	128
#define SB_SILENT	32768
#define SB_POSIXACL	(1<<16)
#define SB_KERNMOUNT	(1<<22)
#define SB_I_VERSION	(1<<23)
#define SB_LAZYTIME	(1<<25)
#define SB_BORN		(1<<29)
#define SB_ACTIVE	(1<<30)
#define SB_I_NOEXEC	0x00000002
#define SB_I_NODEV	0x00000004

enum {
	SB_FREEZE_WRITE	= 1,
	SB_FREEZE_COMPLETE = 4,		
};

#define SB_FREEZE_LEVELS (SB_FREEZE_COMPLETE - 1)

struct sb_writers {
	struct percpu_rw_semaphore	rw_sem[SB_FREEZE_LEVELS];
};

struct super_block {
	struct list_head	s_list;		
	dev_t			s_dev;		
	unsigned char		s_blocksize_bits;
	loff_t			s_maxbytes;
	struct file_system_type	*s_type;
	const struct super_operations	*s_op;
	unsigned long		s_flags;
	unsigned long		s_iflags;	
	struct dentry		*s_root;
	struct rw_semaphore	s_umount;
	int			s_count;
	atomic_t		s_active;
	struct hlist_bl_head	s_roots;
	struct list_head	s_mounts;
	struct backing_dev_info *s_bdi;
	struct hlist_node	s_instances;

	struct sb_writers	s_writers;

	void			*s_fs_info;

	char			s_id[32];

	struct shrinker s_shrink;	

	atomic_long_t s_remove_count;

	struct user_namespace *s_user_ns;

	struct list_lru		s_dentry_lru;
	struct list_lru		s_inode_lru;
	struct rcu_head		rcu;
	struct work_struct	destroy_work;

	spinlock_t		s_inode_list_lock ____cacheline_aligned_in_smp;
	struct list_head	s_inodes;
} __randomize_layout;

static inline struct user_namespace *i_user_ns(const struct inode *inode)
{
	return inode->i_sb->s_user_ns;
}

void inode_init_owner(struct user_namespace *mnt_userns, struct inode *inode,
		      const struct inode *dir, umode_t mode);

struct file_operations {
	struct module *owner;
	ssize_t (*read_iter)(struct kiocb *, struct iov_iter *);
	ssize_t (*write_iter)(struct kiocb *, struct iov_iter *);
	int (*mmap)(struct file *, struct vm_area_struct *);
	unsigned long mmap_supported_flags;
	int (*open)(struct inode *, struct file *);
	int (*release)(struct inode *, struct file *);
	unsigned long (*get_unmapped_area)(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);
} __randomize_layout;

struct inode_operations {
	struct dentry *(*lookup)(struct inode *, struct dentry *, unsigned int);
	int (*permission)(struct user_namespace *, struct inode *, int);

	int (*create) (struct user_namespace *, struct inode *,struct dentry *,
		       umode_t, bool);
} ____cacheline_aligned;
/* call_write_iter inlined at fs/read_write.c - single caller */
/* call_mmap inlined at mm/mmap.c - single caller */

struct super_operations {};

#define S_APPEND	(1 << 2)
#define S_DEAD		(1 << 4)
#define S_SWAPFILE	(1 << 8)
#define S_AUTOMOUNT	(1 << 11)

#define IS_APPEND(inode)	((inode)->i_flags & S_APPEND)
#define IS_DEADDIR(inode)	((inode)->i_flags & S_DEAD)
#define IS_SWAPFILE(inode)	((inode)->i_flags & S_SWAPFILE)
#define IS_AUTOMOUNT(inode)	((inode)->i_flags & S_AUTOMOUNT)

/* HAS_UNMAPPED_ID inlined at fs/namei.c - single caller */

#define __I_NEW			3
#define I_NEW			(1 << __I_NEW)
#define I_WILL_FREE		(1 << 4)
#define I_FREEING		(1 << 5)
#define I_CLEAR			(1 << 6)
#define I_LINKABLE		(1 << 10)

extern void inc_nlink(struct inode *inode);

struct file_system_type {
	const char *name;
	int (*init_fs_context)(struct fs_context *);
	const struct fs_parameter_spec *parameters;
	struct dentry *(*mount) (struct file_system_type *, int,
		       const char *, void *);
	void (*kill_sb) (struct super_block *);
	struct module *owner;
	struct file_system_type * next;
	struct hlist_head fs_supers;

	struct lock_class_key s_writers_key[SB_FREEZE_LEVELS];
};

void generic_shutdown_super(struct super_block *sb);
void kill_anon_super(struct super_block *sb);
void kill_litter_super(struct super_block *sb);
void deactivate_super(struct super_block *sb);
void deactivate_locked_super(struct super_block *sb);
int get_anon_bdev(dev_t *);
void free_anon_bdev(dev_t);
struct super_block *sget_fc(struct fs_context *fc,
			    int (*test)(struct super_block *, struct fs_context *),
			    int (*set)(struct super_block *, struct fs_context *));

#define fops_get(fops) \
	((fops) ? (try_module_get((fops)->owner), (fops)) : NULL)
#define fops_put(fops) \
	do { if (fops) module_put((fops)->owner); } while(0)

#define replace_fops(f, fops) \
	do {	\
		struct file *__file = (f); \
		fops_put(__file->f_op); \
		BUG_ON(!(__file->f_op = (fops))); \
	} while(0)

extern int register_filesystem(struct file_system_type *);

extern void iput(struct inode *);

#define MAX_RW_COUNT (INT_MAX & PAGE_MASK)

struct filename {
	const char *name;
	const __user char *uptr;
	int refcnt;
	const char iname[];
};
static_assert(offsetof(struct filename, iname) % sizeof(long) == 0);

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

extern void init_special_inode(struct inode *, umode_t, dev_t);

int inode_permission(struct user_namespace *, struct inode *, int);
static inline int path_permission(const struct path *path, int mask)
{
	return inode_permission(mnt_user_ns(path->mnt),
				d_inode(path->dentry), mask);
}

/* file_start_write and file_end_write inlined at fs/read_write.c - single caller each */

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

extern unsigned int get_next_ino(void);
extern void evict_inodes(struct super_block *sb);

extern struct inode *new_inode(struct super_block *sb);

static inline void *
alloc_inode_sb(struct super_block *sb, struct kmem_cache *cache, gfp_t gfp)
{
	return kmem_cache_alloc_lru(cache, &sb->s_inode_lru, gfp);
}

/* remove_inode_hash inlined at fs/inode.c - single caller */

extern int generic_file_mmap(struct file *, struct vm_area_struct *);
ssize_t filemap_read(struct kiocb *iocb, struct iov_iter *to,
		ssize_t already_read);
extern ssize_t generic_file_read_iter(struct kiocb *, struct iov_iter *);
extern ssize_t generic_file_write_iter(struct kiocb *, struct iov_iter *);

#define special_file(m) (S_ISCHR(m)||S_ISBLK(m)||S_ISFIFO(m)||S_ISSOCK(m))

/* page_get_link, page_put_link, page_symlink, page_symlink_inode_operations
 * removed - symlinks never created */

extern struct file_system_type *get_filesystem(struct file_system_type *fs);
extern void put_filesystem(struct file_system_type *fs);
extern struct file_system_type *get_fs_type(const char *name);
/* Removed: get_super, get_active_super, drop_super_exclusive,
   iterate_supers, iterate_supers_type, drop_super - never called */

extern const struct address_space_operations ram_aops;

extern struct dentry *simple_lookup(struct inode *, struct dentry *, unsigned int flags);
extern const struct file_operations simple_dir_operations;

#define __FMODE_EXEC		((__force int) FMODE_EXEC)
#define __FMODE_NONOTIFY	((__force int) FMODE_NONOTIFY)

#define ACC_MODE(x) ("\004\002\006\006"[(x)&O_ACCMODE])
#define OPEN_FMODE(flag) ((__force fmode_t)(((flag + 1) & O_ACCMODE) | \
					    (flag & __FMODE_NONOTIFY)))

extern bool path_noexec(const struct path *path);

#endif
