#include <linux/sched/user.h>
#include <linux/user_namespace.h>

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
	.ns.inum = 0xEFFFFFFDU,
	.flags = USERNS_INIT_FLAGS,
};

struct user_struct root_user = {
	.__count = REFCOUNT_INIT(1),
	.uid = GLOBAL_ROOT_UID,
	.ratelimit = RATELIMIT_STATE_INIT(root_user.ratelimit, 0, 0),
};

void free_uid(struct user_struct *up)
{
}
