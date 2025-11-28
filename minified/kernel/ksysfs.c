/* Stub: kernel sysfs simplified for minimal kernel */

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/kexec.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/capability.h>
#include <linux/compiler.h>

#include <linux/rcupdate.h>

struct kobject *kernel_kobj;

static int __init ksysfs_init(void)
{
	kernel_kobj = kobject_create_and_add("kernel", NULL);
	return kernel_kobj ? 0 : -ENOMEM;
}

core_initcall(ksysfs_init);
