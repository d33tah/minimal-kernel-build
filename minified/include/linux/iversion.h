#ifndef _LINUX_IVERSION_H
#define _LINUX_IVERSION_H

#include <linux/fs.h>


#define I_VERSION_QUERIED_SHIFT	(1)
#define I_VERSION_QUERIED	(1ULL << (I_VERSION_QUERIED_SHIFT - 1))
#define I_VERSION_INCREMENT	(1ULL << I_VERSION_QUERIED_SHIFT)

static inline void
inode_set_iversion_raw(struct inode *inode, u64 val)
{
	atomic64_set(&inode->i_version, val);
}

static inline u64
inode_peek_iversion_raw(const struct inode *inode)
{
	return atomic64_read(&inode->i_version);
}

static inline void
inode_set_max_iversion_raw(struct inode *inode, u64 val)
{
	u64 cur, old;

	cur = inode_peek_iversion_raw(inode);
	for (;;) {
		if (cur > val)
			break;
		old = atomic64_cmpxchg(&inode->i_version, cur, val);
		if (likely(old == cur))
			break;
		cur = old;
	}
}

static inline void
inode_set_iversion(struct inode *inode, u64 val)
{
	inode_set_iversion_raw(inode, val << I_VERSION_QUERIED_SHIFT);
}

static inline void
inode_set_iversion_queried(struct inode *inode, u64 val)
{
	inode_set_iversion_raw(inode, (val << I_VERSION_QUERIED_SHIFT) |
				I_VERSION_QUERIED);
}

static inline bool
inode_maybe_inc_iversion(struct inode *inode, bool force)
{
	u64 cur, old, new;

	 
	smp_mb();
	cur = inode_peek_iversion_raw(inode);
	for (;;) {
		 
		if (!force && !(cur & I_VERSION_QUERIED))
			return false;

		 
		new = (cur & ~I_VERSION_QUERIED) + I_VERSION_INCREMENT;

		old = atomic64_cmpxchg(&inode->i_version, cur, new);
		if (likely(old == cur))
			break;
		cur = old;
	}
	return true;
}


static inline void
inode_inc_iversion(struct inode *inode)
{
	inode_maybe_inc_iversion(inode, true);
}

static inline bool
inode_iversion_need_inc(struct inode *inode)
{
	return inode_peek_iversion_raw(inode) & I_VERSION_QUERIED;
}

static inline void
inode_inc_iversion_raw(struct inode *inode)
{
	atomic64_inc(&inode->i_version);
}

static inline u64
inode_peek_iversion(const struct inode *inode)
{
	return inode_peek_iversion_raw(inode) >> I_VERSION_QUERIED_SHIFT;
}

static inline u64
inode_query_iversion(struct inode *inode)
{
	u64 cur, old, new;

	cur = inode_peek_iversion_raw(inode);
	for (;;) {
		 
		if (cur & I_VERSION_QUERIED) {
			 
			smp_mb();
			break;
		}

		new = cur | I_VERSION_QUERIED;
		old = atomic64_cmpxchg(&inode->i_version, cur, new);
		if (likely(old == cur))
			break;
		cur = old;
	}
	return cur >> I_VERSION_QUERIED_SHIFT;
}

static inline u64 time_to_chattr(struct timespec64 *t)
{
	u64 chattr = t->tv_sec;

	chattr <<= 32;
	chattr += t->tv_nsec;
	return chattr;
}

static inline bool
inode_eq_iversion_raw(const struct inode *inode, u64 old)
{
	return inode_peek_iversion_raw(inode) == old;
}

static inline bool
inode_eq_iversion(const struct inode *inode, u64 old)
{
	return inode_peek_iversion(inode) == old;
}
#endif
