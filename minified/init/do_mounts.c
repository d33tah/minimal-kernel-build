#include <linux/sched.h>
/* linux/ctype.h removed - no ctype functions used */
#include <linux/tty.h>
/* linux/swap.h removed - no swap functions used */
#include <linux/init.h>
/* linux/pm.h removed - no pm functions used */
#include <linux/mm.h>
#include <asm/errno.h>
/* Inlined from root_dev.h */
/* linux/major.h removed - empty */
#include <linux/types.h>
#include <linux/kdev_t.h>

/* Root_RAM0 enum removed - never used */

extern dev_t ROOT_DEV;
/* linux/security.h, linux/delay.h removed - unused */
#include <linux/mount.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/initrd.h>
/* linux/async.h removed - no async functions used */
#include <linux/fs_struct.h>
#include <linux/slab.h>
/* Inlined from ramfs.h */
extern int ramfs_init_fs_context(struct fs_context *fc);
/* linux/blkdev.h removed - empty stub */
/* Inlined from init_syscalls.h */
int __init init_mount(const char *dev_name, const char *dir_name,
		      const char *type_page, unsigned long flags,
		      void *data_page);
int __init init_chroot(const char *filename);
int __init init_eaccess(const char *filename);
int __init init_dup(struct file *file);
#include <uapi/linux/mount.h>
/* --- 2026-01-26 01:00 --- Inlined from do_mounts.h */

/* root_mountflags removed - never read after mount_block_root removal */

dev_t ROOT_DEV;

/* Merged from do_mounts_initrd.c */
unsigned long initrd_start, initrd_end;
int initrd_below_start_ok;
phys_addr_t phys_initrd_start __initdata;
unsigned long phys_initrd_size __initdata;

/* name_to_dev_t, do_mount_root, mount_block_root removed -
 * saved_root_name was never written (root= __setup handler removed),
 * so the entire root device mounting path was dead code.
 * The kernel boots via initramfs, not block device root. */

/* name_to_dev_t stub for header declaration */
dev_t name_to_dev_t(const char *name)
{
	return 0;
}

/* fs_is_nodev, mount_nodev_root, mount_root removed - all stubs */

void __init prepare_namespace(void)
{
	wait_for_device_probe();

	/* saved_root_name was never written (root= __setup handler removed),
	 * so root device name check and mount_block_root are dead code.
	 * The kernel boots via initramfs only. */

	init_mount(".", "/", NULL, MS_MOVE, NULL);
	init_chroot(".");
}

/* default_rootfs removed - was empty stub returning 0 */

/* TMPFS not defined - use ramfs */
static int rootfs_init_fs_context(struct fs_context *fc)
{
	return ramfs_init_fs_context(fc);
}

struct file_system_type rootfs_fs_type = {
	.name = "rootfs",
	.init_fs_context = rootfs_init_fs_context,
	.kill_sb = kill_litter_super,
};

/* init_rootfs removed - was empty */
