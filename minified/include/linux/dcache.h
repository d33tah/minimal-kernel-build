/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_DCACHE_H
#define __LINUX_DCACHE_H

#include <linux/rculist_bl.h>
#include <linux/lockref.h>
#include <linux/hash.h>

#define hashlen_len(hashlen) ((u32)((hashlen) >> 32))
#define hashlen_create(hash, len) ((u64)(len) << 32 | (u32)(hash))

/*
 * linux/include/linux/dcache.h
 *
 * Dirent cache data structures
 *
 * (C) Copyright 1997 Thomas Schoebel-Theuer,
 * with heavy changes by Linus Torvalds
 */

#define IS_ROOT(x) ((x) == (x)->d_parent)

#define HASH_LEN_DECLARE \
	u32 hash;        \
	u32 len
#define bytemask_from_count(cnt) (~(~0ul << (cnt) * 8))

/*
 * "quick string" -- eases parameter passing, but more importantly
 * saves "metadata" about the string (ie length and the hash).
 *
 * hash comes first so it snuggles against d_parent in the
 * dentry.
 */
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

#define DNAME_INLINE_LEN 40 /* 128 bytes */

#define d_lock d_lockref.lock

struct dentry {
	/* RCU lookup touched fields */
	unsigned int d_flags; /* protected by d_lock */
	struct hlist_bl_node d_hash; /* lookup hash list */
	struct dentry *d_parent; /* parent directory */
	struct qstr d_name;
	struct inode *d_inode; /* Where the name belongs to - NULL is
					 * negative */
	unsigned char d_iname[DNAME_INLINE_LEN]; /* small names */

	/* Ref lookup also touches following */
	struct lockref d_lockref; /* per-dentry lock and refcount */
	struct super_block *d_sb; /* The root of the dentry tree */
	union {
		struct rcu_head d_rcu;
	} d_u;
} __randomize_layout;

#define DCACHE_ENTRY_TYPE 0x00700000
#define DCACHE_MISS_TYPE 0x00000000
#define DCACHE_DIRECTORY_TYPE 0x00200000 /* Normal directory */
#define DCACHE_REGULAR_TYPE 0x00400000
#define DCACHE_SPECIAL_TYPE 0x00500000

/*
 * These are the low-level FS interfaces to the dcache..
 */
extern void d_instantiate(struct dentry *, struct inode *);

/* allocate/de-allocate */
extern struct dentry *d_alloc(struct dentry *, const struct qstr *);

/* only used at mount-time */
extern struct dentry *d_make_root(struct inode *);

extern void d_add(struct dentry *, struct inode *);

/* appendix may either be NULL or be used for transname suffixes */
extern struct dentry *d_lookup(const struct dentry *, const struct qstr *);

static inline struct dentry *dget(struct dentry *dentry)
{
	if (dentry)
		lockref_get(&dentry->d_lockref);
	return dentry;
}

extern struct dentry *dget_parent(struct dentry *dentry);

/**
 *	d_unhashed -	is dentry hashed
 *	@dentry: entry to check
 *
 *	Returns true if the dentry passed is not currently hashed.
 */

static inline int d_unhashed(const struct dentry *dentry)
{
	return hlist_bl_unhashed(&dentry->d_hash);
}

extern void dput(struct dentry *);

/*
 * Directory cache entry type accessor functions.
 */
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
	return d_can_lookup(dentry);
}

static inline bool d_is_negative(const struct dentry *dentry)
{
	// TODO: check d_is_whiteout(dentry) also.
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

/**
 * d_inode - Get the actual inode of this dentry
 * @dentry: The dentry to query
 *
 * This is the helper normal filesystems should use to get at their own inodes
 * in their own dentries and ignore the layering superimposed upon them.
 */
static inline struct inode *d_inode(const struct dentry *dentry)
{
	return dentry->d_inode;
}

/**
 * d_backing_inode - Get upper or lower inode we should be using
 * @upper: The upper layer
 *
 * This is the helper that should be used to get at the inode that will be used
 * if this dentry were to be opened as a file.  The inode may be on the upper
 * dentry or it may be on a lower dentry pinned by the upper.
 *
 * Normal filesystems should not use this to access their own inodes.
 */
static inline struct inode *d_backing_inode(const struct dentry *upper)
{
	struct inode *inode = upper->d_inode;

	return inode;
}

#endif /* __LINUX_DCACHE_H */
