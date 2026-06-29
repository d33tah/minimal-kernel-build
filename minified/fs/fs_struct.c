#include <linux/sched/task.h>
#include <linux/path.h>
#include <linux/fs_struct.h>

void set_fs_root(struct fs_struct *fs, const struct path *path)
{
	struct path old_root;

	path_get(path);
	spin_lock(&fs->lock);
	write_seqcount_begin(&fs->seq);
	old_root = fs->root;
	fs->root = *path;
	write_seqcount_end(&fs->seq);
	spin_unlock(&fs->lock);
	if (old_root.dentry)
		path_put(&old_root);
}

void set_fs_pwd(struct fs_struct *fs, const struct path *path)
{
	struct path old_pwd;

	path_get(path);
	spin_lock(&fs->lock);
	write_seqcount_begin(&fs->seq);
	old_pwd = fs->pwd;
	fs->pwd = *path;
	write_seqcount_end(&fs->seq);
	spin_unlock(&fs->lock);

	if (old_pwd.dentry)
		path_put(&old_pwd);
}


void exit_fs(struct task_struct *tsk)
{
	/*
	 * RUNTIME-DEAD ANCHOR-STUB: exit_fs drops the dying task's fs_struct.
	 * Both call sites are runtime-dead on this 1-shot boot: do_exit's tail
	 * (init panics on is_global_init() before reaching it, HIT=False) and
	 * copy_process's bad_fork_cleanup_fs rollback (copy_process succeeds for
	 * every spawn -- init + the few kthreads -- so the error path never runs,
	 * HIT=False). No task ever tears down its fs_struct here.
	 */
}

int current_umask(void)
{
	return current->fs->umask;
}

struct fs_struct init_fs = {
	.users		= 1,
	.lock		= __SPIN_LOCK_UNLOCKED(init_fs.lock),
	.seq		= SEQCNT_SPINLOCK_ZERO(init_fs.seq, &init_fs.lock),
	.umask		= 0022,
};
