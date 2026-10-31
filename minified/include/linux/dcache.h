#ifndef __LINUX_DCACHE_H
#define __LINUX_DCACHE_H

#include <linux/rculist_bl.h>
#include <linux/lockref.h>
#include <linux/hash.h>

#define hashlen_len(hashlen) ((u32)((hashlen) >> 32))
#define hashlen_create(hash, len) ((u64)(len) << 32 | (u32)(hash))

#define IS_ROOT(x) ((x) == (x)->d_parent)

/* x86 is unconditionally little-endian (__LITTLE_ENDIAN defined in kconfig.h,
 * force-included every TU; __BIG_ENDIAN never defined) -> big-endian #else dead */
#define HASH_LEN_DECLARE \
	u32 hash;        \
	u32 len
#define bytemask_from_count(cnt) (~(~0ul << (cnt) * 8))

struct qstr {
	union {
		struct {
			HASH_LEN_DECLARE;
		};
		u64 hash_len;
	};
	const unsigned char *name;
};

#define QSTR_INIT(n, l) { { { .len = l } }, .name = n }

#define DNAME_INLINE_LEN 40

#define d_lock d_lockref.lock

struct dentry {
	unsigned int d_flags;
	struct hlist_bl_node d_hash;
	struct dentry *d_parent;
	struct qstr d_name;
	struct inode *d_inode;
	unsigned char d_iname[DNAME_INLINE_LEN];
	struct lockref d_lockref;
	struct super_block *d_sb;
	union {
		struct hlist_node d_alias;
		struct rcu_head d_rcu;
	} d_u;
} __randomize_layout;

/*
 * DCACHE_SHRINK_LIST / DCACHE_MOUNTED / DCACHE_NEED_AUTOMOUNT /
 * DCACHE_MANAGE_TRANSIT / DCACHE_MANAGED_DENTRY are never set on this
 * build -- removed; their branches were folded out.
 */

#define DCACHE_ENTRY_TYPE 0x00700000
#define DCACHE_MISS_TYPE 0x00000000
#define DCACHE_DIRECTORY_TYPE 0x00200000
#define DCACHE_AUTODIR_TYPE 0x00300000
#define DCACHE_REGULAR_TYPE 0x00400000
#define DCACHE_SPECIAL_TYPE 0x00500000

extern void d_instantiate(struct dentry *, struct inode *);

extern struct dentry *d_alloc(struct dentry *, const struct qstr *);
extern struct dentry *d_make_root(struct inode *);

extern void d_add(struct dentry *, struct inode *);

extern struct dentry *d_lookup(const struct dentry *, const struct qstr *);

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

extern void dput(struct dentry *);

static inline unsigned __d_entry_type(const struct dentry *dentry)
{
	return dentry->d_flags & DCACHE_ENTRY_TYPE;
}

static inline bool d_is_miss(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_MISS_TYPE;
}

static inline bool d_can_lookup(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_DIRECTORY_TYPE;
}

static inline bool d_is_dir(const struct dentry *dentry)
{
	/* Simplified: only check d_can_lookup since d_is_autodir was unused */
	return d_can_lookup(dentry);
}

static inline bool d_is_negative(const struct dentry *dentry)
{
	return d_is_miss(dentry);
}

static inline bool d_flags_negative(unsigned flags)
{
	return (flags & DCACHE_ENTRY_TYPE) == DCACHE_MISS_TYPE;
}

static inline bool d_is_positive(const struct dentry *dentry)
{
	return !d_is_negative(dentry);
}

static inline struct inode *d_inode(const struct dentry *dentry)
{
	return dentry->d_inode;
}

static inline struct inode *d_backing_inode(const struct dentry *upper)
{
	struct inode *inode = upper->d_inode;

	return inode;
}

#endif
