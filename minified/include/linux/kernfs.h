/* Minimal kernfs.h - only what's actually used */
#ifndef __LINUX_KERNFS_H
#define __LINUX_KERNFS_H

#include <linux/types.h>
#include <linux/atomic.h>

struct rb_node;
struct rb_root;

/* Only need kernfs_elem_dir for the subdirs field access in core.c */
struct kernfs_elem_dir {
	unsigned long		subdirs;
	struct rb_root		*children;
	struct kernfs_root	*root;
	unsigned long		rev;
};

/* Minimal kernfs_node - only fields that are actually accessed */
struct kernfs_node {
	atomic_t		count;
	atomic_t		active;
	struct kernfs_node	*parent;
	const char		*name;
	struct rb_node		*rb;
	const void		*ns;
	unsigned int		hash;
	/* Only dir is used (via sd->dir.subdirs) */
	struct kernfs_elem_dir	dir;
	void			*priv;
	u64			id;
	unsigned short		flags;
	umode_t			mode;
	void			*iattr;
};

static inline void kernfs_init(void) { }

#endif /* __LINUX_KERNFS_H */
