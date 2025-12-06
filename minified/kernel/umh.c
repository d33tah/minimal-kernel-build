/* Stub usermode helper - no userspace execution */
#include <linux/kmod.h>
#include <linux/init.h>

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
