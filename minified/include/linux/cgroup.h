/* Minimal cgroup.h - stubs for !CONFIG_CGROUPS */
#ifndef _LINUX_CGROUP_H
#define _LINUX_CGROUP_H
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/nodemask.h>
#include <linux/rculist.h>
#include <linux/fs.h>
/* seq_file.h removed - header is empty */
/* --- Inlined from linux/kernfs.h --- */
#ifndef __LINUX_KERNFS_H
#define __LINUX_KERNFS_H
#include <linux/types.h>
#include <linux/atomic.h>
struct rb_node;
struct rb_root;
struct kernfs_elem_dir {
	unsigned long		subdirs;
	struct rb_root		*children;
	struct kernfs_root	*root;
	unsigned long		rev;
};
struct kernfs_node {
	atomic_t		count;
	atomic_t		active;
	struct kernfs_node	*parent;
	const char		*name;
	struct rb_node		*rb;
	const void		*ns;
	unsigned int		hash;
	struct kernfs_elem_dir	dir;
	void			*priv;
	u64			id;
	unsigned short		flags;
	umode_t			mode;
	void			*iattr;
};
#endif /* __LINUX_KERNFS_H */
#include <linux/jump_label.h>
#include <linux/ns_common.h>
#include <linux/nsproxy.h>
#include <linux/user_namespace.h>
#include <linux/refcount.h>
/* kernel_stat.h removed - empty */
/* struct kernel_clone_args, cgroup_subsys_state, cgroup forward decls removed - unused */
struct cgroup_namespace { struct ns_common ns; struct user_namespace *user_ns; struct ucounts *ucounts; struct css_set *root_cset; };
static inline void free_cgroup_ns(struct cgroup_namespace *ns) { }
/* copy_cgroup_ns removed - no callers */
static inline void put_cgroup_ns(struct cgroup_namespace *ns) { if (ns && refcount_dec_and_test(&ns->ns.count)) free_cgroup_ns(ns); }
#endif
