
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/key.h>
#include <linux/sched/user.h>
#include <linux/interrupt.h>
#include <linux/export.h>
#include <linux/user_namespace.h>
#include <linux/proc_ns.h>

struct user_namespace init_user_ns = {
	.uid_map = {
		.nr_extents = 1,
		{
			.extent[0] = {
				.first = 0,
				.lower_first = 0,
				.count = 4294967295U,
			},
		},
	},
	.gid_map = {
		.nr_extents = 1,
		{
			.extent[0] = {
				.first = 0,
				.lower_first = 0,
				.count = 4294967295U,
			},
		},
	},
	.projid_map = {
		.nr_extents = 1,
		{
			.extent[0] = {
				.first = 0,
				.lower_first = 0,
				.count = 4294967295U,
			},
		},
	},
	.ns.count = REFCOUNT_INIT(3),
	.owner = GLOBAL_ROOT_UID,
	.group = GLOBAL_ROOT_GID,
	.ns.inum = PROC_USER_INIT_INO,
	.flags = USERNS_INIT_FLAGS,
};

/* UIDHASH_BITS, UIDHASH_SZ, __uidhashfn, uidhashentry, uidhash_table removed - never used */

static struct kmem_cache *uid_cachep;

static DEFINE_SPINLOCK(uidhash_lock);

struct user_struct root_user = {
	.__count = REFCOUNT_INIT(1),
	.uid = GLOBAL_ROOT_UID,
	.ratelimit = RATELIMIT_STATE_INIT(root_user.ratelimit, 0, 0),
};

/* uid_hash_insert removed - never called */

static void uid_hash_remove(struct user_struct *up)
{
	hlist_del_init(&up->uidhash_node);
}

/* uid_hash_find removed - only caller was alloc_uid */
/* user_epoll_alloc removed - never called */

static void user_epoll_free(struct user_struct *up)
{
}

static void free_user(struct user_struct *up, unsigned long flags)
	__releases(&uidhash_lock)
{
	uid_hash_remove(up);
	spin_unlock_irqrestore(&uidhash_lock, flags);
	user_epoll_free(up);
	kmem_cache_free(uid_cachep, up);
}

void free_uid(struct user_struct *up)
{
	unsigned long flags;

	if (!up)
		return;

	if (refcount_dec_and_lock_irqsave(&up->__count, &uidhash_lock, &flags))
		free_user(up, flags);
}

/* alloc_uid removed - never called in minimal kernel */

/* uid_cache_init removed - kmem_cache_create hangs with low memory */
