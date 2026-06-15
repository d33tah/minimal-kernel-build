
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/sched/user.h>
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


struct user_struct root_user = {
	.__count	= REFCOUNT_INIT(1),
	.uid		= GLOBAL_ROOT_UID,
	.ratelimit	= RATELIMIT_STATE_INIT(root_user.ratelimit, 0, 0),
};

/*
 * The only user_struct in this kernel is the static root_user: alloc_uid()
 * is never called, so every cred->user points at &root_user (see INIT_USER /
 * prepare_creds). root_user therefore must never be kmem_cache_free()d, and
 * its refcount never reaches 0 (init_cred holds a permanent reference). So
 * free_uid() only needs to balance get_uid()'s refcount_inc(); the freeing
 * path (uid cache, uid hash) is unreachable and removed.
 */
void free_uid(struct user_struct *up)
{
	if (!up)
		return;

	refcount_dec_and_test(&up->__count);
}
