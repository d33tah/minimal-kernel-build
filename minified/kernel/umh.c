#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/binfmts.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/cred.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fs_struct.h>
#include <linux/workqueue.h>
#include <linux/mount.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <linux/rwsem.h>
#include <linux/ptrace.h>
#include <linux/async.h>
#include <linux/uaccess.h>
#include <linux/initrd.h>

int usermodehelper_read_trylock(void)
{
	return 0;
}

void usermodehelper_read_unlock(void)
{
}

void __usermodehelper_set_disable_depth(enum umh_disable_depth depth)
{
}

int __usermodehelper_disable(enum umh_disable_depth depth)
{
	return 0;
}

int call_usermodehelper_exec(struct subprocess_info *sub_info, int wait)
{
	if (sub_info->cleanup)
		(*sub_info->cleanup)(sub_info);
	return -ENOENT;
}

int call_usermodehelper(const char *path, char **argv, char **envp, int wait)
{
	return -ENOENT;
}

struct subprocess_info *call_usermodehelper_setup(const char *path, char **argv,
		char **envp, gfp_t gfp_mask,
		int (*init)(struct subprocess_info *info, struct cred *new),
		void (*cleanup)(struct subprocess_info *), void *data)
{
	return NULL;
}

void __init usermodehelper_init(void)
{
}
