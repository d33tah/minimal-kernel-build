
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

void put_ucounts(struct ucounts *ucounts)
{
}

struct ucounts *alloc_ucounts(struct user_namespace *ns, kuid_t uid)
{
	return &init_ucounts;
}

struct ucounts *inc_ucount(struct user_namespace *ns, kuid_t uid,
			   enum ucount_type type)
{
	return &init_ucounts;
}

void dec_ucount(struct ucounts *ucounts, enum ucount_type type)
{
}

long inc_rlimit_ucounts(struct ucounts *ucounts, enum ucount_type type, long v)
{
	return 0;
}

static __init int user_namespace_sysctl_init(void)
{
	return 0;
}
subsys_initcall(user_namespace_sysctl_init);
