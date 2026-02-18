#include <linux/types.h>

extern dev_t ROOT_DEV;
#include <linux/device.h>
#include <linux/fs.h>
extern int ramfs_init_fs_context(struct fs_context *fc);
int __init init_mount(const char *dev_name, const char *dir_name,
		      const char *type_page, unsigned long flags,
		      void *data_page);
int __init init_chroot(const char *filename);
int __init init_eaccess(const char *filename);
int __init init_dup(struct file *file);
#include <uapi/linux/mount.h>

dev_t ROOT_DEV;

/* Merged from do_mounts_initrd.c */
unsigned long initrd_start, initrd_end;
int initrd_below_start_ok;
phys_addr_t phys_initrd_start __initdata;
unsigned long phys_initrd_size __initdata;

/* name_to_dev_t stub for header declaration */
dev_t name_to_dev_t(const char *name)
{
	return 0;
}

void __init prepare_namespace(void)
{
	wait_for_device_probe();

	init_mount(".", "/", NULL, MS_MOVE, NULL);
	init_chroot(".");
}

static int rootfs_init_fs_context(struct fs_context *fc)
{
	return ramfs_init_fs_context(fc);
}

struct file_system_type rootfs_fs_type = {
	.name = "rootfs",
	.init_fs_context = rootfs_init_fs_context,
	.kill_sb = kill_litter_super,
};
