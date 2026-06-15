/* Minimal cgroup.h - stubs for !CONFIG_CGROUPS */
#ifndef _LINUX_CGROUP_H
#define _LINUX_CGROUP_H

#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/nodemask.h>
#include <linux/rculist.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/kernfs.h>
#include <linux/jump_label.h>
#include <linux/types.h>
#include <linux/ns_common.h>
#include <linux/nsproxy.h>
#include <linux/user_namespace.h>
#include <linux/refcount.h>
#include <linux/kernel_stat.h>

static inline void cgroup_fork(struct task_struct *p) {}
static inline void cgroup_free(struct task_struct *p) {}

#endif /* _LINUX_CGROUP_H */
