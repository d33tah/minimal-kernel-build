#include <linux/cred.h>
#include <linux/slab.h>
#include <linux/sched/coredump.h>

#include <linux/init_task.h>
#include <linux/security.h>

static struct kmem_cache *cred_jar;

static struct group_info init_groups = { .usage = ATOMIC_INIT(2) };

struct cred init_cred = { .usage			= ATOMIC_INIT(4), .uid			= GLOBAL_ROOT_UID, .gid			= GLOBAL_ROOT_GID, .euid			= GLOBAL_ROOT_UID, .egid			= GLOBAL_ROOT_GID, .fsuid			= GLOBAL_ROOT_UID, .fsgid			= GLOBAL_ROOT_GID, .cap_permitted		= CAP_FULL_SET, .user			= INIT_USER, .user_ns		= &init_user_ns, .group_info		= &init_groups, .ucounts		= &init_ucounts, };

void __put_cred(struct cred *cred)
{
	/*
	 * RUNTIME-DEAD ANCHOR-STUB: the cred kref-release root. Reached only via
	 * put_cred() when ->usage drops to 0, which never happens on this 1-shot
	 * boot (init_cred holds permanent refs; no cred is ever fully released).
	 * HIT=False. Private rcu callback put_cred_rcu (group_info/uid/ucounts/
	 * user_ns release + kmem_cache_free) cascaded away. Symbol kept for the
	 * put_cred() inline in cred.h.
	 */
}

void exit_creds(struct task_struct *tsk)
{
	/*
	 * RUNTIME-DEAD ANCHOR-STUB: drops a dying task's cred refs. Both call sites
	 * are runtime-dead on this 1-shot boot: __put_task_struct (HIT=False -- no
	 * task is ever fully released) and copy_process's bad_fork rollback (dead).
	 */
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
	inc_rlimit_ucounts(task_ucounts(p), UCOUNT_RLIMIT_NPROC, 1);
	return 0;
}

static bool cred_cap_issubset(const struct cred *set, const struct cred *subset)
{
	const struct user_namespace *set_ns = set->user_ns;
	const struct user_namespace *subset_ns = subset->user_ns;

	 
	if (set_ns == subset_ns)
		return cap_issubset(subset->cap_permitted, set->cap_permitted);

	 
	for (;subset_ns != &init_user_ns; subset_ns = subset_ns->parent) {
		if ((set_ns == subset_ns->parent)  && uid_eq(subset_ns->owner, set->euid))
			return true;
	}

	return false;
}

int commit_creds(struct cred *new)
{
	struct task_struct *task = current;
	const struct cred *old = task->real_cred;

	BUG_ON(task->cred != old);
	BUG_ON(atomic_read(&new->usage) < 1);

	get_cred(new);  

	 
	if (!uid_eq(old->euid, new->euid) || !gid_eq(old->egid, new->egid) || !uid_eq(old->fsuid, new->fsuid) || !gid_eq(old->fsgid, new->fsgid) || !cred_cap_issubset(old, new)) {
		if (task->mm)
			set_dumpable(task->mm, 0);

		smp_wmb();
	}


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

	 
	if (old_ucounts->ns == new->user_ns && uid_eq(old_ucounts->uid, new->uid))
		return 0;

	if (!(new_ucounts = alloc_ucounts(new->user_ns, new->uid)))
		return -EAGAIN;

	new->ucounts = new_ucounts;
	put_ucounts(old_ucounts);

	return 0;
}

void __init cred_init(void)
{
	 
	cred_jar = kmem_cache_create("cred_jar", sizeof(struct cred), 0, SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT, NULL);
}


