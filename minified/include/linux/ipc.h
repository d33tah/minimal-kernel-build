#ifndef _LINUX_IPC_H
#define _LINUX_IPC_H

#include <linux/spinlock.h>
#include <linux/uidgid.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

/* --- 2025-12-06 17:24 --- rhashtable-types.h inlined */
struct rhash_head {
	struct rhash_head __rcu		*next;
};

struct rhlist_head {
	struct rhash_head		rhead;
	struct rhlist_head __rcu	*next;
};

struct bucket_table;

struct rhashtable_compare_arg {
	struct rhashtable *ht;
	const void *key;
};

typedef u32 (*rht_hashfn_t)(const void *data, u32 len, u32 seed);
typedef u32 (*rht_obj_hashfn_t)(const void *data, u32 len, u32 seed);
typedef int (*rht_obj_cmpfn_t)(struct rhashtable_compare_arg *arg,
			       const void *obj);

struct rhashtable_params {
	u16			nelem_hint;
	u16			key_len;
	u16			key_offset;
	u16			head_offset;
	unsigned int		max_size;
	u16			min_size;
	bool			automatic_shrinking;
	rht_hashfn_t		hashfn;
	rht_obj_hashfn_t	obj_hashfn;
	rht_obj_cmpfn_t		obj_cmpfn;
};

struct rhashtable {
	struct bucket_table __rcu	*tbl;
	unsigned int			key_len;
	unsigned int			max_elems;
	struct rhashtable_params	p;
	bool				rhlist;
	struct work_struct		run_work;
	struct mutex                    mutex;
	spinlock_t			lock;
	atomic_t			nelems;
};

struct rhltable {
	struct rhashtable ht;
};

struct rhashtable_walker {
	struct list_head list;
	struct bucket_table *tbl;
};

struct rhashtable_iter {
	struct rhashtable *ht;
	struct rhash_head *p;
	struct rhlist_head *list;
	struct rhashtable_walker walker;
	unsigned int slot;
	unsigned int skip;
	bool end_of_table;
};

int rhashtable_init(struct rhashtable *ht,
		    const struct rhashtable_params *params);
int rhltable_init(struct rhltable *hlt,
		  const struct rhashtable_params *params);
/* --- end rhashtable-types.h inlined --- */
#include <linux/refcount.h>
#include <asm/posix_types.h>

/* From uapi/asm-generic/ipcbuf.h - inlined */
struct ipc64_perm {
	__kernel_key_t		key;
	__kernel_uid32_t	uid;
	__kernel_gid32_t	gid;
	__kernel_uid32_t	cuid;
	__kernel_gid32_t	cgid;
	__kernel_mode_t		mode;
	unsigned char		__pad1[4 - sizeof(__kernel_mode_t)];
	unsigned short		seq;
	unsigned short		__pad2;
	__kernel_ulong_t	__unused1;
	__kernel_ulong_t	__unused2;
};

/* From uapi/linux/ipc.h - inlined */
struct ipc_perm
{
	__kernel_key_t	key;
	__kernel_uid_t	uid;
	__kernel_gid_t	gid;
	__kernel_uid_t	cuid;
	__kernel_gid_t	cgid;
	__kernel_mode_t	mode;
	unsigned short	seq;
};

struct kern_ipc_perm {
	spinlock_t	lock;
	bool		deleted;
	int		id;
	key_t		key;
	kuid_t		uid;
	kgid_t		gid;
	kuid_t		cuid;
	kgid_t		cgid;
	umode_t		mode;
	unsigned long	seq;
	void		*security;

	struct rhash_head khtnode;

	struct rcu_head rcu;
	refcount_t refcount;
} ____cacheline_aligned_in_smp __randomize_layout;

#endif  
