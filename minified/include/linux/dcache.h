#ifndef __LINUX_DCACHE_H
#define __LINUX_DCACHE_H

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/math.h>
#include <linux/rculist.h>
#include <linux/rculist_bl.h>
#include <linux/spinlock.h>
#include <linux/seqlock.h>
#include <linux/cache.h>
#include <linux/rcupdate.h>
/* Inlined from lockref.h */
#include <generated/bounds.h>
struct lockref { spinlock_t lock; int count; };
extern void lockref_get(struct lockref *);
extern int lockref_put_return(struct lockref *);
extern int lockref_get_not_zero(struct lockref *);
extern void lockref_mark_dead(struct lockref *);
extern int lockref_get_not_dead(struct lockref *);
#include <linux/hash.h>

#define hashlen_hash(hashlen) ((u32)(hashlen))
#define hashlen_len(hashlen)  ((u32)((hashlen) >> 32))
#define hashlen_create(hash, len) ((u64)(len)<<32 | (u32)(hash))

#include <linux/wait.h>

/* struct path, vfsmount forward decls removed - unused */

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
	const struct dentry_operations *d_op;
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

struct dentry_operations {
	/* d_revalidate, d_weak_revalidate, d_hash, d_compare removed - never called/set */
	int (*d_delete)(const struct dentry *);
	/* d_init, d_release, d_prune, d_iput, d_dname, d_automount, d_manage, d_real removed - never called/set */
} ____cacheline_aligned;

#define DCACHE_OP_HASH			0x00000001
#define DCACHE_OP_COMPARE		0x00000002
/* DCACHE_OP_REVALIDATE removed - unused */
#define DCACHE_OP_DELETE		0x00000008
/* DCACHE_OP_PRUNE removed - unused */

/* DCACHE_DISCONNECTED, DCACHE_REFERENCED, DCACHE_DONTCACHE removed - unused */

#define DCACHE_CANT_MOUNT		0x00000100
#define DCACHE_SHRINK_LIST		0x00000400
/* DCACHE_OP_WEAK_REVALIDATE removed - unused */

#define DCACHE_DENTRY_KILLED		0x00008000

#define DCACHE_MOUNTED			0x00010000
#define DCACHE_NEED_AUTOMOUNT		0x00020000
/* DCACHE_MANAGE_TRANSIT inlined into DCACHE_MANAGED_DENTRY */
#define DCACHE_MANAGED_DENTRY \
	(DCACHE_MOUNTED|DCACHE_NEED_AUTOMOUNT|0x00040000)

#define DCACHE_LRU_LIST			0x00080000

#define DCACHE_ENTRY_TYPE		0x00700000
#define DCACHE_MISS_TYPE		0x00000000
#define DCACHE_DIRECTORY_TYPE		0x00200000  
#define DCACHE_AUTODIR_TYPE		0x00300000  
#define DCACHE_REGULAR_TYPE		0x00400000  
#define DCACHE_SPECIAL_TYPE		0x00500000
/* DCACHE_SYMLINK_TYPE removed - never used */

#define DCACHE_MAY_FREE			0x00800000
#define DCACHE_FALLTHRU			0x01000000
/* DCACHE_OP_REAL removed - unused */

#define DCACHE_PAR_LOOKUP		0x10000000  
#define DCACHE_NORCU			0x40000000  

extern void d_instantiate(struct dentry *, struct inode *);
/* __d_drop, d_drop made static - only used in dcache.c */
/* d_delete removed - never called */
/* d_set_d_op removed - d_op never read */

/* d_alloc made static - only used in dcache.c */
/* d_alloc_anon removed - inlined into d_make_root */
extern struct dentry * d_alloc_parallel(struct dentry *, const struct qstr *,
					wait_queue_head_t *);
/* shrink_dcache_parent removed - never called */
extern void shrink_dcache_for_umount(struct super_block *);
/* d_invalidate removed - never called */

extern struct dentry * d_make_root(struct inode *);

/* d_tmpfile removed - tmpfile callback removed from inode_operations */

extern void d_add(struct dentry *, struct inode *);
/* d_ancestor removed - never called */
extern struct dentry *d_lookup(const struct dentry *, const struct qstr *);
extern struct dentry *__d_lookup(const struct dentry *, const struct qstr *);
extern struct dentry *__d_lookup_rcu(const struct dentry *parent,
				const struct qstr *name, unsigned *seq);

/* d_path and dynamic_dname removed - fs/d_path.c was removed */

static inline struct dentry *dget(struct dentry *dentry)
{
	if (dentry)
		lockref_get(&dentry->d_lockref);
	return dentry;
}

extern struct dentry *dget_parent(struct dentry *dentry);

static inline int d_unhashed(const struct dentry *dentry)
{
	return hlist_bl_unhashed(&dentry->d_hash);
}

static inline int d_unlinked(const struct dentry *dentry)
{
	return d_unhashed(dentry) && !IS_ROOT(dentry);
}

/* cant_mount removed - inlined at single call site */

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

static inline unsigned __d_entry_type(const struct dentry *dentry)
{
	return dentry->d_flags & DCACHE_ENTRY_TYPE;
}

/* d_is_miss removed - never called */

static inline bool d_can_lookup(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_DIRECTORY_TYPE;
}

/* d_is_dir removed - alias for d_can_lookup, use it directly */
/* d_is_symlink, d_is_reg, d_flags_negative removed - inlined at single call site */
/* d_really_is_positive removed - never called */

extern int sysctl_vfs_cache_pressure;
/* vfs_pressure_ratio removed - inlined at single call site */

static inline struct inode *d_inode(const struct dentry *dentry)
{
	return dentry->d_inode;
}

/* d_backing_inode removed - alias for d_inode in minimal kernel (no overlayfs) */

#endif
