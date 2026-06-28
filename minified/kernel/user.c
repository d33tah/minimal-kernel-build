
#include <linux/sched/user.h>
#include <linux/user_namespace.h>

struct user_namespace init_user_ns = {
	.owner = GLOBAL_ROOT_UID,
};


struct user_struct root_user = {
	.__count	= REFCOUNT_INIT(1),
};

