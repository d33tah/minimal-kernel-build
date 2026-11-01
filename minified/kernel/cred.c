// SPDX-License-Identifier: GPL-2.0-or-later
/* Task credentials management - see Documentation/security/credentials.rst
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#include <linux/init_task.h>

/* init to 2 - one for init_task, one to ensure it is never freed */
static struct group_info init_groups = { .usage = ATOMIC_INIT(2) };

/*
 * The initial credentials for the initial task
 */
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

static struct cred *prepare_creds(void)
{
	struct task_struct *task = current;
	const struct cred *old;
	struct cred *new;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
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

/*
 * Prepare credentials for current to perform an execve()
 * - The caller must hold ->cred_guard_mutex
 */
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

/*
 * Copy credentials for the new process created by fork()
 *
 * We share if we can, but under some circumstances we have to generate a new
 * set.
 *
 * The new process gets the current process's subjective credentials as its
 * objective and subjective credentials
 */
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

/**
 * commit_creds - Install new credentials upon the current task
 * @new: The credentials to be assigned
 *
 * Install a new set of credentials to the current task, using RCU to replace
 * the old set.  Both the objective and the subjective credentials pointers are
 * updated.  This function may not be called if the subjective credentials are
 * in an overridden state.
 *
 * This function eats the caller's reference to the new credentials.
 *
 * Always returns 0 thus allowing this function to be tail-called at the end
 * of, say, sys_setgid().
 */
int commit_creds(struct cred *new)
{
	struct task_struct *task = current;
	const struct cred *old = task->real_cred;

	BUG_ON(task->cred != old);
	BUG_ON(atomic_read(&new->usage) < 1);

	get_cred(new); /* we will require a ref for the subj creds too */

	rcu_assign_pointer(task->real_cred, new);
	rcu_assign_pointer(task->cred, new);

	/* release the old obj and subj refs both */
	put_cred(old);
	put_cred(old);
	return 0;
}

/**
 * abort_creds - Discard a set of credentials and unlock the current task
 * @new: The credentials that were going to be applied
 *
 * Discard a set of credentials that were under construction and unlock the
 * current task.
 */
void abort_creds(struct cred *new)
{
	BUG_ON(atomic_read(&new->usage) < 1);
	put_cred(new);
}
