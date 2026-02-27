
#include <linux/slab.h>
#include <linux/user_namespace.h>

struct ucounts init_ucounts = {
	.ns = &init_user_ns,
	.uid = GLOBAL_ROOT_UID,
	.count = ATOMIC_INIT(1),
};

/* Stubbed for hello-world kernel: single user, no resource limits */

struct ucounts *get_ucounts(struct ucounts *ucounts)
{
	return ucounts;
}

long inc_rlimit_ucounts(struct ucounts *ucounts, enum ucount_type type, long v)
{
	return 0;
}
