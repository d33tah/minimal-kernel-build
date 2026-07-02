#ifndef _LINUX_PATH_H
#define _LINUX_PATH_H

struct dentry;
struct vfsmount;

struct path {
	struct vfsmount *mnt;
	struct dentry *dentry;
} __randomize_layout;

extern void path_get(const struct path *);
extern void path_put(const struct path *);

#endif   
