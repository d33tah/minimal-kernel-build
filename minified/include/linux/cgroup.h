/* Minimal cgroup.h - stubs for !CONFIG_CGROUPS */
#ifndef _LINUX_CGROUP_H
#define _LINUX_CGROUP_H
#include <linux/fs.h>
#ifndef __LINUX_KERNFS_H
#define __LINUX_KERNFS_H
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
#include <linux/user_namespace.h>
struct cgroup_namespace { struct ns_common ns; struct user_namespace *user_ns; struct ucounts *ucounts; struct css_set *root_cset; };
#endif
