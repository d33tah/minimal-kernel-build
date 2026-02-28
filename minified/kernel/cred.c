#include <linux/cred.h>
#include <linux/slab.h>
#include <linux/init_task.h>

static struct kmem_cache *cred_jar;

static struct group_info init_groups = { .usage = ATOMIC_INIT(2) };

struct ucounts init_ucounts = {
	.ns = &init_user_ns,
	.uid = GLOBAL_ROOT_UID,
	.count = ATOMIC_INIT(1),
};

struct cred init_cred = {
	.usage = ATOMIC_INIT(4),
	.euid = GLOBAL_ROOT_UID,
	.user = INIT_USER,
	.user_ns = &init_user_ns,
	.group_info = &init_groups,
	.ucounts = &init_ucounts,
};

void __put_cred(struct cred *cred)
{
	kmem_cache_free(cred_jar, cred);
}

struct cred *prepare_creds(void)
{
	struct cred *new;
	new = kmem_cache_alloc(cred_jar, GFP_KERNEL);
	if (!new)
		return NULL;
	memcpy(new, current->cred, sizeof(struct cred));
	atomic_set(&new->usage, 1);
	return new;
}

int copy_creds(struct task_struct *p)
{
	struct cred *new = prepare_creds();
	if (!new)
		return -ENOMEM;
	p->cred = p->real_cred = get_cred(new);
	return 0;
}

int commit_creds(struct cred *new)
{
	struct task_struct *task = current;
	const struct cred *old = task->real_cred;

	get_cred(new);
	rcu_assign_pointer(task->real_cred, new);
	rcu_assign_pointer(task->cred, new);
	put_cred(old);
	put_cred(old);
	return 0;
}

void __init cred_init(void)
{
	cred_jar = kmem_cache_create(
		"cred_jar", sizeof(struct cred), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);
}
