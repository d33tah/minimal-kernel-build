/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Credentials management - see Documentation/security/credentials.rst
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _LINUX_CRED_H
#define _LINUX_CRED_H

#include <linux/sched/user.h>

/*
 * COW Supplementary groups list
 */
struct group_info {
	atomic_t usage;
} __randomize_layout;

/**
 * get_group_info - Get a reference to a group info structure
 * @group_info: The group info to reference
 *
 * This gets a reference to a set of supplementary groups.
 *
 * If the caller is accessing a task's credentials, they must hold the RCU read
 * lock when reading.
 */
static inline struct group_info *get_group_info(struct group_info *gi)
{
	atomic_inc(&gi->usage);
	return gi;
}

/*
 * The security context of a task
 *
 * The parts of the context break down into two categories:
 *
 *  (1) The objective context of a task.  These parts are used when some other
 *	task is attempting to affect this one.
 *
 *  (2) The subjective context.  These details are used when the task is acting
 *	upon another object, be that a file, a task, a key or whatever.
 *
 * Note that some members of this structure belong to both categories - the
 * LSM security pointer for instance.
 *
 * A task has two security pointers.  task->real_cred points to the objective
 * context that defines that task's actual details.  The objective part of this
 * context is used whenever that task is acted upon.
 *
 * task->cred points to the subjective context that defines the details of how
 * that task is going to act upon another object.  This may be overridden
 * temporarily to point to another security context, but normally points to the
 * same context as task->real_cred.
 */
struct cred {
	atomic_t usage;
	kuid_t uid; /* real UID of the task */
	kgid_t gid; /* real GID of the task */
	kuid_t euid; /* effective UID of the task */
	kgid_t egid; /* effective GID of the task */
	kuid_t fsuid; /* UID for VFS ops */
	kgid_t fsgid; /* GID for VFS ops */
	struct user_struct *user; /* real user ID subscription */
	struct user_namespace *user_ns;
	struct group_info *group_info; /* supplementary groups for euid/fsgid */
	/* RCU deletion */
	union {
	};
} __randomize_layout;

extern int copy_creds(struct task_struct *, unsigned long);
extern struct cred *prepare_exec_creds(void);
extern int commit_creds(struct cred *);
extern void abort_creds(struct cred *);

/**
 * get_new_cred - Get a reference on a new set of credentials
 * @cred: The new credentials to reference
 *
 * Get a reference on the specified set of new credentials.  The caller must
 * release the reference.
 */
static inline struct cred *get_new_cred(struct cred *cred)
{
	atomic_inc(&cred->usage);
	return cred;
}

/**
 * get_cred - Get a reference on a set of credentials
 * @cred: The credentials to reference
 *
 * Get a reference on the specified set of credentials.  The caller must
 * release the reference.  If %NULL is passed, it is returned with no action.
 *
 * This is used to deal with a committed set of credentials.  Although the
 * pointer is const, this will temporarily discard the const and increment the
 * usage count.  The purpose of this is to attempt to catch at compile time the
 * accidental alteration of a set of credentials that should be considered
 * immutable.
 */
static inline const struct cred *get_cred(const struct cred *cred)
{
	struct cred *nonconst_cred = (struct cred *)cred;
	if (!cred)
		return cred;
	return get_new_cred(nonconst_cred);
}

/**
 * put_cred - Release a reference to a set of credentials
 * @cred: The credentials to release
 *
 * Release a reference to a set of credentials, deleting them when the last ref
 * is released.  If %NULL is passed, nothing is done.
 *
 * This takes a const pointer to a set of credentials because the credentials
 * on task_struct are attached by const pointers to prevent accidental
 * alteration of otherwise immutable credential sets.
 */
static inline void put_cred(const struct cred *_cred)
{
	struct cred *cred = (struct cred *)_cred;

	if (cred)
		atomic_dec(&cred->usage);
}

#define current_cred() rcu_dereference_protected(current->cred, 1)

#define get_current_cred() (get_cred(current_cred()))

#define current_cred_xxx(xxx) ({ current_cred()->xxx; })

#define current_fsuid() (current_cred_xxx(fsuid))
#define current_fsgid() (current_cred_xxx(fsgid))

extern struct user_namespace init_user_ns;
static inline struct user_namespace *current_user_ns(void)
{
	return &init_user_ns;
}

#endif
