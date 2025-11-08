#include <linux/init.h>
#include <linux/stat.h>
#include <linux/kdev_t.h>
#include <linux/syscalls.h>

static int __init default_rootfs(void)
{
	return 0;
}

late_initcall(default_rootfs);