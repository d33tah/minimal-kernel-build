/* Minimal cgroup.h - stubs for !CONFIG_CGROUPS */
#ifndef _LINUX_CGROUP_H
#define _LINUX_CGROUP_H
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/nodemask.h>
#include <linux/rculist.h>
#include <linux/fs.h>
/* seq_file.h removed - header is empty */
#include <linux/kernfs.h>
#include <linux/jump_label.h>
#include <linux/types.h>
#include <linux/ns_common.h>
#include <linux/nsproxy.h>
#include <linux/user_namespace.h>
#include <linux/refcount.h>
#include <linux/kernel_stat.h>
/* struct kernel_clone_args, cgroup_subsys_state, cgroup forward decls removed - unused */
struct cgroup_namespace { struct ns_common ns; struct user_namespace *user_ns; struct ucounts *ucounts; struct css_set *root_cset; };
static inline void free_cgroup_ns(struct cgroup_namespace *ns) { }
/* copy_cgroup_ns removed - no callers */
static inline void put_cgroup_ns(struct cgroup_namespace *ns) { if (ns && refcount_dec_and_test(&ns->ns.count)) free_cgroup_ns(ns); }
#endif
