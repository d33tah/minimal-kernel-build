
#ifndef _LINUX_FS_H
#define _LINUX_FS_H

/* wait_bit.h inlined */
#include <linux/wait.h>
struct wait_bit_key {
	void			*flags;
	int			bit_nr;
};
#define __WAIT_BIT_KEY_INITIALIZER(word, bit)					\
	{ .flags = word, .bit_nr = bit, }
void wake_up_bit(void *word, int bit);
extern void __init wait_bit_init(void);
#include <linux/kdev_t.h>
/* rculist_bl.h inlined */
#include <linux/kernel.h>
#include <linux/atomic.h>
#include <linux/bug.h>
static inline void bit_spin_lock(int bitnum, unsigned long *addr)
{
	preempt_disable();
	__acquire(bitlock);
}
static inline void __bit_spin_unlock(int bitnum, unsigned long *addr)
{
	preempt_enable();
	__release(bitlock);
}
#include <linux/rcupdate.h>

#define LIST_BL_LOCKMASK	0UL

struct hlist_bl_head {
	struct hlist_bl_node *first;
};

struct hlist_bl_node {
	struct hlist_bl_node *next, **pprev;
};
#define INIT_HLIST_BL_HEAD(ptr) \
	((ptr)->first = NULL)

static inline void INIT_HLIST_BL_NODE(struct hlist_bl_node *h)
{
	h->next = NULL;
	h->pprev = NULL;
}

#define hlist_bl_entry(ptr, type, member) container_of(ptr,type,member)

static inline bool  hlist_bl_unhashed(const struct hlist_bl_node *h)
{
	return !h->pprev;
}

static inline struct hlist_bl_node *hlist_bl_first(struct hlist_bl_head *h)
{
	return (struct hlist_bl_node *)
		((unsigned long)h->first & ~LIST_BL_LOCKMASK);
}

static inline bool hlist_bl_empty(const struct hlist_bl_head *h)
{
	return !((unsigned long)READ_ONCE(h->first) & ~LIST_BL_LOCKMASK);
}

static inline void __hlist_bl_del(struct hlist_bl_node *n)
{
	struct hlist_bl_node *next = n->next;
	struct hlist_bl_node **pprev = n->pprev;

	WRITE_ONCE(*pprev,
		   (struct hlist_bl_node *)
			((unsigned long)next |
			 ((unsigned long)*pprev & LIST_BL_LOCKMASK)));
	if (next)
		next->pprev = pprev;
}

static inline void hlist_bl_lock(struct hlist_bl_head *b)
{
	bit_spin_lock(0, (unsigned long *)b);
}

static inline void hlist_bl_unlock(struct hlist_bl_head *b)
{
	__bit_spin_unlock(0, (unsigned long *)b);
}

static inline void hlist_bl_set_first_rcu(struct hlist_bl_head *h,
					struct hlist_bl_node *n)
{
	rcu_assign_pointer(h->first,
		(struct hlist_bl_node *)((unsigned long)n | LIST_BL_LOCKMASK));
}

static inline struct hlist_bl_node *hlist_bl_first_rcu(struct hlist_bl_head *h)
{
	return (struct hlist_bl_node *)
		((unsigned long)rcu_dereference_check(h->first, 1) & ~LIST_BL_LOCKMASK);
}

static inline void hlist_bl_add_head_rcu(struct hlist_bl_node *n,
					struct hlist_bl_head *h)
{
	struct hlist_bl_node *first;

	first = hlist_bl_first(h);

	n->next = first;
	if (first)
		first->pprev = &n->next;
	n->pprev = &h->first;

	hlist_bl_set_first_rcu(h, n);
}
#define hlist_bl_for_each_entry_rcu(tpos, pos, head, member)		\
	for (pos = hlist_bl_first_rcu(head);				\
		pos &&							\
		({ tpos = hlist_bl_entry(pos, typeof(*tpos), member); 1; }); \
		pos = rcu_dereference_raw(pos->next))
/* end rculist_bl.h */

#include <linux/seqlock.h>
struct lockref { spinlock_t lock; int count; };
extern void lockref_get(struct lockref *);
extern int lockref_put_return(struct lockref *);
extern void lockref_mark_dead(struct lockref *);
/* hash.h inlined */
#include <asm/types.h>
static inline u32 __hash_32(u32 val)
{
	return val * 0x61C88647;
}
static inline u32 hash_32(u32 val, unsigned int bits)
{
	return __hash_32(val) >> (32 - bits);
}
#define hash_long(val, bits) hash_32(val, bits)
static inline u32 hash_ptr(const void *ptr, unsigned int bits)
{
	return hash_32((unsigned long)ptr, bits);
}

#define hashlen_len(hashlen)  ((u32)((hashlen) >> 32))
#define hashlen_create(hash, len) ((u64)(len)<<32 | (u32)(hash))

#define IS_ROOT(x) ((x) == (x)->d_parent)

struct qstr {
	union {
		struct {
			u32 hash;
			u32 len;
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
	 	struct rcu_head d_rcu;
	} d_u;
} __randomize_layout;

#define DCACHE_DENTRY_KILLED		0x00008000

#define DCACHE_ENTRY_TYPE		0x00700000
#define DCACHE_MISS_TYPE		0x00000000
#define DCACHE_DIRECTORY_TYPE		0x00200000
#define DCACHE_REGULAR_TYPE		0x00400000
#define DCACHE_SPECIAL_TYPE		0x00500000

#define DCACHE_FALLTHRU			0x01000000

#define DCACHE_PAR_LOOKUP		0x10000000


extern struct dentry * d_alloc_parallel(struct dentry *, const struct qstr *,
					wait_queue_head_t *);
extern void shrink_dcache_for_umount(struct super_block *);

extern struct dentry * d_make_root(struct inode *);

extern void d_add(struct dentry *, struct inode *);
extern struct dentry *d_lookup(const struct dentry *, const struct qstr *);
extern struct dentry *__d_lookup(const struct dentry *, const struct qstr *);
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

static inline bool d_can_lookup(const struct dentry *dentry) {
  return (dentry->d_flags & DCACHE_ENTRY_TYPE) == DCACHE_DIRECTORY_TYPE;
}

static inline struct inode *d_inode(const struct dentry *dentry)
{
	return dentry->d_inode;
}
/* end dcache.h inline */
#ifndef _LINUX_PATH_H
#define _LINUX_PATH_H
struct path { struct vfsmount *mnt; struct dentry *dentry; } __randomize_layout;
extern void path_get(const struct path *);
extern void path_put(const struct path *);
#endif
#include <linux/list_lru.h>
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
extern void down(struct semaphore *sem);
extern void up(struct semaphore *sem);
#include <linux/fcntl.h>
/* percpu-rwsem.h inlined */
#include <linux/percpu.h>
#include <linux/sched/signal.h>
struct rcuwait { struct task_struct __rcu *task; };
struct rcu_sync { int gp_state; wait_queue_head_t gp_wait; };
#include <linux/lockdep.h>
struct percpu_rw_semaphore {
	struct rcu_sync		rss;
	unsigned int __percpu	*read_count;
	struct rcuwait		writer;
	wait_queue_head_t	waiters;
	atomic_t		block;
};

/* mount.h inlined */
#define MNT_NOEXEC	0x04
#define MNT_INTERNAL	0x4000
#define MNT_LOCKED	0x800000
struct vfsmount {
	struct dentry *mnt_root;
	struct super_block *mnt_sb;
	int mnt_flags;
} __randomize_layout;
extern void mntput(struct vfsmount *mnt);
extern struct vfsmount *mntget(struct vfsmount *mnt);
extern int __mnt_want_write(struct vfsmount *);
extern void __mnt_drop_write(struct vfsmount *);
#include <linux/slab.h>
#define INR_OPEN_CUR 1024
#define INR_OPEN_MAX 4096
/* end uapi/linux/fs.h */

struct backing_dev_info;
struct kiocb;
struct vm_area_struct;
struct cred;
struct iov_iter;
struct fs_context;
extern void __init inode_init(void);
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

#define FMODE_PATH		((__force fmode_t)0x4000)

#define FMODE_ATOMIC_POS	((__force fmode_t)0x8000)

#define FMODE_WRITER		((__force fmode_t)0x10000)

#define FMODE_CAN_READ          ((__force fmode_t)0x20000)

#define FMODE_CAN_WRITE         ((__force fmode_t)0x40000)

#define FMODE_OPENED		((__force fmode_t)0x80000)
#define FMODE_CREATED		((__force fmode_t)0x100000)

#define FMODE_STREAM		((__force fmode_t)0x200000)
#define FMODE_NONOTIFY		((__force fmode_t)0x4000000)

#define AOP_TRUNCATED_PAGE 0x80001

struct kiocb {
	struct file		*ki_filp;

	randomized_struct_fields_start

	loff_t			ki_pos;
	randomized_struct_fields_end
};

struct address_space_operations {
	int (*read_folio)(struct file *, struct folio *);
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

struct inode {
	umode_t			i_mode;
	unsigned short		i_opflags;
	unsigned int		i_flags;

	const struct inode_operations	*i_op;
	struct super_block	*i_sb;
	struct address_space	*i_mapping;

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

struct file {
	union {
		struct rcu_head 	fu_rcuhead;
	} f_u;
	struct path		f_path;
	struct inode		*f_inode;	
	const struct file_operations	*f_op;

	atomic_long_t		f_count;
	unsigned int 		f_flags;
	fmode_t			f_mode;

	loff_t			f_pos;
	struct address_space	*f_mapping;
} __randomize_layout
  __attribute__((aligned(4)));

static inline struct file *get_file(struct file *f)
{
	atomic_long_inc(&f->f_count);
	return f;
}
#define	MAX_NON_LFS	((1UL<<31) - 1)

/* 32-bit only kernel */
#define MAX_LFS_FILESIZE	((loff_t)ULONG_MAX << PAGE_SHIFT)

static inline struct inode *file_inode(const struct file *f)
{
	return f->f_inode;
}

#define SB_KERNMOUNT	(1<<22)
#define SB_BORN		(1<<29)
#define SB_ACTIVE	(1<<30)
#define SB_I_NOEXEC	0x00000002
#define SB_I_NODEV	0x00000004

#define SB_FREEZE_COMPLETE 4

#define SB_FREEZE_LEVELS (SB_FREEZE_COMPLETE - 1)

struct sb_writers {
	struct percpu_rw_semaphore	rw_sem[SB_FREEZE_LEVELS];
};

struct super_block {
	struct list_head	s_list;		
	dev_t			s_dev;		
	loff_t			s_maxbytes;
	struct file_system_type	*s_type;
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

struct file_operations {
	struct module *owner;
	ssize_t (*read_iter)(struct kiocb *, struct iov_iter *);
	ssize_t (*write_iter)(struct kiocb *, struct iov_iter *);
	int (*mmap)(struct file *, struct vm_area_struct *);
	int (*open)(struct inode *, struct file *);
	int (*release)(struct inode *, struct file *);
} __randomize_layout;

struct inode_operations {
	struct dentry *(*lookup)(struct inode *, struct dentry *, unsigned int);
} ____cacheline_aligned;

#define S_DEAD		(1 << 4)
#define IS_DEADDIR(inode)	((inode)->i_flags & S_DEAD)

#define __I_NEW			3
#define I_FREEING		(1 << 5)
#define I_CLEAR			(1 << 6)

extern void inc_nlink(struct inode *inode);

struct file_system_type {
	const char *name;
	int (*init_fs_context)(struct fs_context *);
	void (*kill_sb) (struct super_block *);
	struct module *owner;
	struct file_system_type * next;
	struct hlist_head fs_supers;

	struct lock_class_key s_writers_key[SB_FREEZE_LEVELS];
};

void kill_litter_super(struct super_block *sb);
void deactivate_super(struct super_block *sb);
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

	int refcnt;
	const char iname[];
};
static_assert(offsetof(struct filename, iname) % sizeof(long) == 0);

extern struct file *filp_open(const char *, int, umode_t);

extern struct filename *getname_kernel(const char *);
extern void putname(struct filename *name);

extern void __init vfs_caches_init_early(void);
extern void __init vfs_caches_init(void);

extern struct kmem_cache *names_cachep;

#define __getname()		kmem_cache_alloc(names_cachep, GFP_KERNEL)
#define __putname(name)		kmem_cache_free(names_cachep, (void *)(name))

extern const struct file_operations def_chr_fops;

static inline int path_permission(const struct path *path, int mask)
{
	return 0;
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

extern void evict_inodes(struct super_block *sb);

extern struct inode *new_inode(struct super_block *sb);

static inline void *
alloc_inode_sb(struct super_block *sb, struct kmem_cache *cache, gfp_t gfp)
{
	return kmem_cache_alloc_lru(cache, &sb->s_inode_lru, gfp);
}

extern ssize_t generic_file_read_iter(struct kiocb *, struct iov_iter *);

#define special_file(m) (S_ISCHR(m)||S_ISBLK(m)||S_ISFIFO(m)||S_ISSOCK(m))

extern struct file_system_type *get_filesystem(struct file_system_type *fs);
extern void put_filesystem(struct file_system_type *fs);
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
