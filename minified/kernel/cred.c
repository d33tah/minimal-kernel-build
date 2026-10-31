
#include <linux/init_task.h>

static struct kmem_cache *cred_jar;

static struct group_info init_groups = { .usage = ATOMIC_INIT(2) };

struct cred init_cred = {
	.usage = ATOMIC_INIT(4),
	.uid = GLOBAL_ROOT_UID,
	.gid = GLOBAL_ROOT_GID,
	.euid = GLOBAL_ROOT_UID,
	.egid = GLOBAL_ROOT_GID,
	.fsuid = GLOBAL_ROOT_UID,
	.fsgid = GLOBAL_ROOT_GID,
	.user = INIT_USER,
	.user_ns = &init_user_ns,
	.group_info = &init_groups,
};

struct cred *prepare_creds(void)
{
	struct task_struct *task = current;
	const struct cred *old;
	struct cred *new;

	new = kmem_cache_alloc(cred_jar, GFP_KERNEL);
	if (!new)
		return NULL;

	old = task->cred;
	memcpy(new, old, sizeof(struct cred));

	atomic_set(&new->usage, 1);
	get_group_info(new->group_info);
	get_uid(new->user);
	get_user_ns(new->user_ns);

	return new;
}

struct cred *prepare_exec_creds(void)
{
	struct cred *new;

	new = prepare_creds();
	if (!new)
		return new;

	new->fsuid = new->euid;
	new->fsgid = new->egid;

	return new;
}

int copy_creds(struct task_struct *p, unsigned long clone_flags)
{
	struct cred *new;

	/*
	 * Neither CLONE_THREAD nor CLONE_NEWUSER is ever set on this build (the
	 * only spawns pass CLONE_FS|CLONE_FILES|CLONE_VM|CLONE_UNTRACED|SIGCHLD),
	 * so the thread-share and new-user-namespace paths are both unreachable.
	 */
	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	p->cred = p->real_cred = get_cred(new);
	return 0;
}

int commit_creds(struct cred *new)
{
	struct task_struct *task = current;
	const struct cred *old = task->real_cred;

	BUG_ON(task->cred != old);
	BUG_ON(atomic_read(&new->usage) < 1);

	get_cred(new);

	rcu_assign_pointer(task->real_cred, new);
	rcu_assign_pointer(task->cred, new);

	put_cred(old);
	put_cred(old);
	return 0;
}

void abort_creds(struct cred *new)
{
	BUG_ON(atomic_read(&new->usage) < 1);
	put_cred(new);
}

void __init cred_init(void)
{
	cred_jar = kmem_cache_create(
		"cred_jar", sizeof(struct cred), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);
}
