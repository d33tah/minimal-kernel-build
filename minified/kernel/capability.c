// SPDX-License-Identifier: GPL-2.0
/*
 * linux/kernel/capability.c
 *
 * Copyright (C) 1997  Andrew Main <zefram@fysh.org>
 *
 * Integrated into 2.1.97+,  Andrew G. Morgan <morgan@kernel.org>
 * 30 May 2002:	Cleanup, Robert M. Love <rml@tech9.net>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/capability.h>
#include <asm/bug.h>
#include <linux/export.h>
#include <linux/security.h>

#include "linux/capability.h"
#include "linux/cred.h"
#include "linux/fs.h"
#include "linux/init.h"
#include "linux/rcupdate.h"
#include "linux/sched.h"
#include "linux/stddef.h"
#include "linux/types.h"
#include "linux/uidgid.h"

struct user_namespace;

/*
 * Leveraged for setting/resetting capabilities
 */

const kernel_cap_t __cap_empty_set = CAP_EMPTY_SET;
EXPORT_SYMBOL(__cap_empty_set);

int file_caps_enabled = 1;

static int __init file_caps_disable(char *str)
{
	file_caps_enabled = 0;
	return 1;
}
__setup("no_file_caps", file_caps_disable);


/**
 * file_ns_capable - Determine if the file's opener had a capability in effect
 * @file:  The file we want to check
 * @ns:  The usernamespace we want the capability in
 * @cap: The capability to be tested for
 *
 * Return true if task that opened the file had a capability in effect
 * when the file was opened.
 *
 * This does not set PF_SUPERPRIV because the caller may not
 * actually be privileged.
 */
bool file_ns_capable(const struct file *file, struct user_namespace *ns,
		     int cap)
{

	if (WARN_ON_ONCE(!cap_valid(cap)))
		return false;

	if (security_capable(file->f_cred, ns, cap, CAP_OPT_NONE) == 0)
		return true;

	return false;
}
EXPORT_SYMBOL(file_ns_capable);

/**
 * privileged_wrt_inode_uidgid - Do capabilities in the namespace work over the inode?
 * @ns: The user namespace in question
 * @inode: The inode in question
 *
 * Return true if the inode uid and gid are within the namespace.
 */
bool privileged_wrt_inode_uidgid(struct user_namespace *ns,
				 struct user_namespace *mnt_userns,
				 const struct inode *inode)
{
	return kuid_has_mapping(ns, i_uid_into_mnt(mnt_userns, inode)) &&
	       kgid_has_mapping(ns, i_gid_into_mnt(mnt_userns, inode));
}

/**
 * capable_wrt_inode_uidgid - Check nsown_capable and uid and gid mapped
 * @inode: The inode in question
 * @cap: The capability in question
 *
 * Return true if the current task has the given capability targeted at
 * its own user namespace and that the given inode's uid and gid are
 * mapped into the current user namespace.
 */
bool capable_wrt_inode_uidgid(struct user_namespace *mnt_userns,
			      const struct inode *inode, int cap)
{
	struct user_namespace *ns = current_user_ns();

	return ns_capable(ns, cap) &&
	       privileged_wrt_inode_uidgid(ns, mnt_userns, inode);
}
EXPORT_SYMBOL(capable_wrt_inode_uidgid);

/**
 * ptracer_capable - Determine if the ptracer holds CAP_SYS_PTRACE in the namespace
 * @tsk: The task that may be ptraced
 * @ns: The user namespace to search for CAP_SYS_PTRACE in
 *
 * Return true if the task that is ptracing the current task had CAP_SYS_PTRACE
 * in the specified user namespace.
 */
bool ptracer_capable(struct task_struct *tsk, struct user_namespace *ns)
{
	int ret = 0;  /* An absent tracer adds no restrictions */
	const struct cred *cred;

	rcu_read_lock();
	cred = rcu_dereference(tsk->ptracer_cred);
	if (cred)
		ret = security_capable(cred, ns, CAP_SYS_PTRACE,
				       CAP_OPT_NOAUDIT);
	rcu_read_unlock();
	return (ret == 0);
}
