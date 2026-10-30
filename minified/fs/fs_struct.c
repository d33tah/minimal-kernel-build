#include <linux/sched/task.h>
#include <linux/fs_struct.h>

void set_fs_root(struct fs_struct *fs, const struct path *path)
{
	struct path old_root;

	path_get(path);
	spin_lock(&fs->lock);
	old_root = fs->root;
	fs->root = *path;
	spin_unlock(&fs->lock);
	if (old_root.dentry)
		path_put(&old_root);
}

void set_fs_pwd(struct fs_struct *fs, const struct path *path)
{
	struct path old_pwd;

	path_get(path);
	spin_lock(&fs->lock);
	old_pwd = fs->pwd;
	fs->pwd = *path;
	spin_unlock(&fs->lock);

	if (old_pwd.dentry)
		path_put(&old_pwd);
}

int current_umask(void)
{
	return current->fs->umask;
}

struct fs_struct init_fs = {
	.users = 1,
	.lock = __SPIN_LOCK_UNLOCKED(init_fs.lock),
	.umask = 0022,
};
