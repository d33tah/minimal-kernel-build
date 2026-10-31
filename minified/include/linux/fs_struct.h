#ifndef _LINUX_FS_STRUCT_H
#define _LINUX_FS_STRUCT_H

#include <linux/path.h>

struct fs_struct {
	spinlock_t lock;
	int umask;
	struct path root, pwd;
} __randomize_layout;

extern void set_fs_root(struct fs_struct *, const struct path *);
extern void set_fs_pwd(struct fs_struct *, const struct path *);

static inline void get_fs_root(struct fs_struct *fs, struct path *root)
{
	spin_lock(&fs->lock);
	*root = fs->root;
	path_get(root);
	spin_unlock(&fs->lock);
}

static inline void get_fs_pwd(struct fs_struct *fs, struct path *pwd)
{
	spin_lock(&fs->lock);
	*pwd = fs->pwd;
	path_get(pwd);
	spin_unlock(&fs->lock);
}

#endif
