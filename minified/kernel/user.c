
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sched/user.h>
/* linux/interrupt.h removed - no interrupt features used */
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

static struct kmem_cache *uid_cachep;

static DEFINE_SPINLOCK(uidhash_lock);

struct user_struct root_user = {
	.__count = REFCOUNT_INIT(1),
	.uid = GLOBAL_ROOT_UID,
	.ratelimit = RATELIMIT_STATE_INIT(root_user.ratelimit, 0, 0),
};

/* free_user inlined into free_uid */
void free_uid(struct user_struct *up)
{
	unsigned long flags;

	if (!up)
		return;

	if (refcount_dec_and_lock_irqsave(&up->__count, &uidhash_lock,
					  &flags)) {
		hlist_del_init(&up->uidhash_node);
		spin_unlock_irqrestore(&uidhash_lock, flags);
		kmem_cache_free(uid_cachep, up);
	}
}

/* uid_cache_init removed - kmem_cache_create hangs with low memory */
