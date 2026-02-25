#ifndef _LINUX_FS_STRUCT_H
#define _LINUX_FS_STRUCT_H

#ifndef _LINUX_PATH_H
#define _LINUX_PATH_H
struct dentry;
struct vfsmount;
struct path { struct vfsmount *mnt; struct dentry *dentry; } __randomize_layout;
extern void path_get(const struct path *);
extern void path_put(const struct path *);
#endif
#include <linux/seqlock.h>

struct fs_struct {
	int users;
	spinlock_t lock;
	seqcount_spinlock_t seq;
	struct path root, pwd;
} __randomize_layout;

extern void set_fs_root(struct fs_struct *, const struct path *);
extern void set_fs_pwd(struct fs_struct *, const struct path *);
#endif  
