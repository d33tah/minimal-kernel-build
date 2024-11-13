/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SECCOMP_H
#define _LINUX_SECCOMP_H

#include <uapi/linux/seccomp.h>

#define SECCOMP_FILTER_FLAG_MASK	(SECCOMP_FILTER_FLAG_TSYNC | \
					 SECCOMP_FILTER_FLAG_LOG | \
					 SECCOMP_FILTER_FLAG_SPEC_ALLOW | \
					 SECCOMP_FILTER_FLAG_NEW_LISTENER | \
					 SECCOMP_FILTER_FLAG_TSYNC_ESRCH | \
					 SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV)

/* sizeof() the first published struct seccomp_notif_addfd */
#define SECCOMP_NOTIFY_ADDFD_SIZE_VER0 24
#define SECCOMP_NOTIFY_ADDFD_SIZE_LATEST SECCOMP_NOTIFY_ADDFD_SIZE_VER0


#include <linux/errno.h>

struct seccomp { };
struct seccomp_filter { };
struct seccomp_data;

static inline int secure_computing(void) { return 0; }
static inline int __secure_computing(const struct seccomp_data *sd) { return 0; }

static inline long prctl_get_seccomp(void)
{
	return -EINVAL;
}

static inline long prctl_set_seccomp(unsigned long arg2, char __user *arg3)
{
	return -EINVAL;
}

static inline int seccomp_mode(struct seccomp *s)
{
	return SECCOMP_MODE_DISABLED;
}

#ifdef CONFIG_SECCOMP_FILTER
extern void seccomp_filter_release(struct task_struct *tsk);
extern void get_seccomp_filter(struct task_struct *tsk);
#else  /* CONFIG_SECCOMP_FILTER */
static inline void seccomp_filter_release(struct task_struct *tsk)
{
	return;
}
static inline void get_seccomp_filter(struct task_struct *tsk)
{
	return;
}
#endif /* CONFIG_SECCOMP_FILTER */

static inline long seccomp_get_filter(struct task_struct *task,
				      unsigned long n, void __user *data)
{
	return -EINVAL;
}
static inline long seccomp_get_metadata(struct task_struct *task,
					unsigned long filter_off,
					void __user *data)
{
	return -EINVAL;
}

#ifdef CONFIG_SECCOMP_CACHE_DEBUG
struct seq_file;

int proc_pid_seccomp_cache(struct seq_file *m, struct pid_namespace *ns,
			   struct pid *pid, struct task_struct *task);
#endif
#endif /* _LINUX_SECCOMP_H */
