#include <linux/module.h>
#include <linux/sched.h>
#include <linux/ctype.h>

#include <linux/tty.h>
#include <linux/suspend.h>
#include <linux/root_dev.h>
#include <linux/security.h>
#include <linux/delay.h>
#include <linux/mount.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/initrd.h>
#include <linux/async.h>
#include <linux/fs_struct.h>
#include <linux/slab.h>
#include <linux/ramfs.h>

#include <uapi/linux/mount.h>

#include "do_mounts.h"

int root_mountflags = MS_RDONLY | MS_SILENT;

dev_t ROOT_DEV;



/* Stub: root= cmdline not needed for minimal kernel */
static int __init root_dev_setup(char *line) { return 1; }
__setup("root=", root_dev_setup);

/* Stub: rootwait cmdline not needed for minimal kernel */
static int __init rootwait_setup(char *str) { return 1; }
__setup("rootwait", rootwait_setup);

/* Stub: rootflags= not needed for minimal kernel */
static int __init root_data_setup(char *str) { return 1; }

/* Stub: rootfstype= not needed for minimal kernel */
static int __init fs_names_setup(char *str) { return 1; }

/* Stub: rootdelay= not needed for minimal kernel */
static int __init root_delay_setup(char *str) { return 1; }

__setup("rootflags=", root_data_setup);
__setup("rootfstype=", fs_names_setup);
__setup("rootdelay=", root_delay_setup);

static int rootfs_init_fs_context(struct fs_context *fc)
{
	return ramfs_init_fs_context(fc);
}

struct file_system_type rootfs_fs_type = {
	.name		= "rootfs",
	.init_fs_context = rootfs_init_fs_context,
	.kill_sb	= kill_litter_super,
};

void __init init_rootfs(void)
{
}
