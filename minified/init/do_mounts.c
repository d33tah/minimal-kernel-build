
#include <linux/mount.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/ramfs.h>

/*
 * The root=/rootwait/rootflags=/rootfstype=/rootdelay= command-line options
 * are irrelevant for this initramfs-only kernel (it never mounts a real root
 * device) and the boot command line is empty. The __setup handlers were empty
 * `return 1;` stubs that merely swallowed those args; any such arg, if ever
 * passed, is now handled by unknown_bootoption (a no-op with PRINTK off).
 */

static int rootfs_init_fs_context(struct fs_context *fc)
{
	return ramfs_init_fs_context(fc);
}

struct file_system_type rootfs_fs_type = {
	.name		= "rootfs",
	.init_fs_context = rootfs_init_fs_context,
	.kill_sb	= kill_litter_super,
};
