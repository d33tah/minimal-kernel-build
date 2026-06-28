
#include <linux/sched/user.h>
#include <linux/user_namespace.h>

struct user_namespace init_user_ns = {
	.owner = GLOBAL_ROOT_UID,
};


struct user_struct root_user = {
	.__count	= REFCOUNT_INIT(1),
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
