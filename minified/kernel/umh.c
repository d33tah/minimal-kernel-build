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
