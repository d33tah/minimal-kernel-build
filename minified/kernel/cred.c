#include <linux/cred.h>
#include <linux/slab.h>
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
	.cap_permitted = CAP_FULL_SET,
	.user = INIT_USER,
	.user_ns = &init_user_ns,
	.group_info = &init_groups,
	.ucounts = &init_ucounts,
};

static void put_cred_rcu(struct rcu_head *rcu)
{
	struct cred *cred = container_of(rcu, struct cred, rcu);

	if (atomic_read(&cred->usage) != 0)
		panic("CRED: put_cred_rcu() sees %p with usage %d\n", cred,
		      atomic_read(&cred->usage));

	if (cred->group_info)
		put_group_info(cred->group_info);
	free_uid(cred->user);
	if (cred->ucounts)
		put_ucounts(cred->ucounts);
	put_user_ns(cred->user_ns);
	kmem_cache_free(cred_jar, cred);
}

void __put_cred(struct cred *cred)
{
	BUG_ON(atomic_read(&cred->usage) != 0);
	BUG_ON(cred == current->cred);
	BUG_ON(cred == current->real_cred);

	if (cred->non_rcu)
		put_cred_rcu(&cred->rcu);
	else
		call_rcu(&cred->rcu, put_cred_rcu);
}

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

	new->non_rcu = 0;
	atomic_set(&new->usage, 1);
	get_group_info(new->group_info);
	get_uid(new->user);
	get_user_ns(new->user_ns);

	new->ucounts = get_ucounts(new->ucounts);
	if (!new->ucounts)
		goto error;

	return new;

error:
	abort_creds(new);
	return NULL;
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

int copy_creds(struct task_struct *p)
{
	struct cred *new;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	p->cred = p->real_cred = get_cred(new);
	inc_rlimit_ucounts(task_ucounts(p), UCOUNT_RLIMIT_NPROC, 1);
	return 0;
}

int commit_creds(struct cred *new)
{
	struct task_struct *task = current;
	const struct cred *old = task->real_cred;

	BUG_ON(task->cred != old);
	BUG_ON(atomic_read(&new->usage) < 1);

	get_cred(new);

	if (new->user != old->user || new->user_ns != old->user_ns)
		inc_rlimit_ucounts(new->ucounts, UCOUNT_RLIMIT_NPROC, 1);
	rcu_assign_pointer(task->real_cred, new);
	rcu_assign_pointer(task->cred, new);
	if (new->user != old->user || new->user_ns != old->user_ns)
		dec_rlimit_ucounts(old->ucounts, UCOUNT_RLIMIT_NPROC, 1);

	put_cred(old);
	put_cred(old);
	return 0;
}

void abort_creds(struct cred *new)
{
	BUG_ON(atomic_read(&new->usage) < 1);
	put_cred(new);
}

int set_cred_ucounts(struct cred *new)
{
	struct ucounts *new_ucounts, *old_ucounts = new->ucounts;

	if (old_ucounts->ns == new->user_ns &&
	    uid_eq(old_ucounts->uid, new->uid))
		return 0;

	if (!(new_ucounts = alloc_ucounts(new->user_ns, new->uid)))
		return -EAGAIN;

	new->ucounts = new_ucounts;
	put_ucounts(old_ucounts);

	return 0;
}

void __init cred_init(void)
{
	cred_jar = kmem_cache_create(
		"cred_jar", sizeof(struct cred), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);
}
