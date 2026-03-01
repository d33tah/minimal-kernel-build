#include <linux/cred.h>
#include <linux/user_namespace.h>

struct user_namespace init_user_ns = {
	.ns.count = REFCOUNT_INIT(3),
	.ns.inum = 0xEFFFFFFDU,
};
