/* Stub usermode helper - no userspace execution */
#include <linux/kmod.h>
#include <linux/init.h>

void __usermodehelper_set_disable_depth(enum umh_disable_depth depth)
{
}

int __usermodehelper_disable(enum umh_disable_depth depth)
{
	return 0;
}
