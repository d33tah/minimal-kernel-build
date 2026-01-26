#include <linux/module.h>
#include <linux/sched.h>
#include <linux/ctype.h>
#include <linux/tty.h>
#include <linux/swap.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/mm.h>
#include <asm/errno.h>
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
#include <linux/shmem_fs.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/init_syscalls.h>
#include <uapi/linux/mount.h>
/* --- 2026-01-26 01:00 --- Inlined from do_mounts.h */

int root_mountflags = MS_RDONLY | MS_SILENT;
static char *__initdata root_device_name;
static char __initdata saved_root_name[64];
static int root_wait;

dev_t ROOT_DEV;

/* Merged from do_mounts_initrd.c */
unsigned long initrd_start, initrd_end;
int initrd_below_start_ok;
phys_addr_t phys_initrd_start __initdata;
unsigned long phys_initrd_size __initdata;

/* devt_from_devnum inlined into name_to_dev_t */

dev_t name_to_dev_t(const char *name)
{
	unsigned maj, min, offset;
	dev_t devt = 0;
	char *p, dummy;

	if (strcmp(name, "/dev/nfs") == 0)
		return Root_NFS;
	if (strcmp(name, "/dev/cifs") == 0)
		return Root_CIFS;
	if (strcmp(name, "/dev/ram") == 0)
		return Root_RAM0;
	/* devt_from_devnum inlined */
	if (sscanf(name, "%u:%u%c", &maj, &min, &dummy) == 2 ||
	    sscanf(name, "%u:%u:%u:%c", &maj, &min, &offset, &dummy) == 3) {
		devt = MKDEV(maj, min);
		if (maj != MAJOR(devt) || min != MINOR(devt))
			return 0;
	} else {
		devt = new_decode_dev(simple_strtoul(name, &p, 16));
		if (*p)
			return 0;
	}
	return devt;
}

/* Stub: root= cmdline not needed for minimal kernel */
static int __init root_dev_setup(char *line)
{
	return 1;
}
__setup("root=", root_dev_setup);

/* Stub: rootwait cmdline not needed for minimal kernel */
static int __init rootwait_setup(char *str)
{
	return 1;
}
__setup("rootwait", rootwait_setup);

static char *__initdata root_mount_data;
/* Stub: rootflags= not needed for minimal kernel */
static int __init root_data_setup(char *str)
{
	return 1;
}

/* root_fs_names, root_delay removed - stubs never assigned them */
/* Stub: rootfstype= not needed for minimal kernel */
static int __init fs_names_setup(char *str)
{
	return 1;
}
/* Stub: rootdelay= not needed for minimal kernel */
static int __init root_delay_setup(char *str)
{
	return 1;
}

__setup("rootflags=", root_data_setup);
__setup("rootfstype=", fs_names_setup);
__setup("rootdelay=", root_delay_setup);

/* split_fs_names removed - only caller mount_nodev_root was removed */

static int __init do_mount_root(const char *name, const char *fs,
				const int flags, const void *data)
{
	struct super_block *s;
	struct page *p = NULL;
	char *data_page = NULL;
	int ret;

	if (data) {
		p = alloc_page(GFP_KERNEL);
		if (!p)
			return -ENOMEM;
		data_page = page_address(p);

		strncpy(data_page, data, PAGE_SIZE);
	}

	ret = init_mount(name, "/root", fs, flags, data_page);
	if (ret)
		goto out;

	init_chdir("/root");
	s = current->fs->pwd.dentry->d_sb;
	ROOT_DEV = s->s_dev;
	printk(KERN_INFO
	       "VFS: Mounted root (%s filesystem)%s on device %u:%u.\n",
	       s->s_type->name, sb_rdonly(s) ? " readonly" : "",
	       MAJOR(ROOT_DEV), MINOR(ROOT_DEV));

out:
	if (p)
		put_page(p);
	return ret;
}

void __init mount_block_root(char *name, int flags)
{
	struct page *page = alloc_page(GFP_KERNEL);
	char *fs_names = page_address(page);
	char *p;
	char b[BDEVNAME_SIZE];
	int num_fs, i;

	scnprintf(b, BDEVNAME_SIZE, "unknown-block(%u,%u)", MAJOR(ROOT_DEV),
		  MINOR(ROOT_DEV));
	/* root_fs_names always NULL - stub never assigns it */
	num_fs = list_bdev_fs_names(fs_names, PAGE_SIZE);
retry:
	for (i = 0, p = fs_names; i < num_fs; i++, p += strlen(p) + 1) {
		int err;

		if (!*p)
			continue;
		err = do_mount_root(name, p, flags, root_mount_data);
		switch (err) {
		case 0:
			goto out;
		case -EACCES:
		case -EINVAL:
			continue;
		}

		printk("VFS: Cannot open root device \"%s\" or %s: error %d\n",
		       root_device_name, b, err);
		printk("Please append a correct \"root=\" boot option; here are the available partitions:\n");

		printk_all_partitions();
		panic("VFS: Unable to mount root fs on %s", b);
	}
	if (!(flags & SB_RDONLY)) {
		flags |= SB_RDONLY;
		goto retry;
	}

	printk("List of all partitions:\n");
	printk_all_partitions();
	printk("No filesystem could mount root, tried: ");
	for (i = 0, p = fs_names; i < num_fs; i++, p += strlen(p) + 1)
		printk(" %s", p);
	printk("\n");
	panic("VFS: Unable to mount root fs on %s", b);
out:
	put_page(page);
}

/* fs_is_nodev, mount_nodev_root removed - root_fs_names always NULL */

void __init mount_root(void)
{
	/* root_fs_names always NULL - mount_nodev_root never called */
}

void __init prepare_namespace(void)
{
	/* root_delay always 0 - stub never assigns it */

	wait_for_device_probe();

	if (saved_root_name[0]) {
		root_device_name = saved_root_name;
		if (!strncmp(root_device_name, "mtd", 3) ||
		    !strncmp(root_device_name, "ubi", 3)) {
			mount_block_root(root_device_name, root_mountflags);
			goto out;
		}
		ROOT_DEV = name_to_dev_t(root_device_name);
		if (strncmp(root_device_name, "/dev/", 5) == 0)
			root_device_name += 5;
	}

	/* initrd_load() always returns false, call removed */

	if ((ROOT_DEV == 0) && root_wait) {
		printk(KERN_INFO "Waiting for root device %s...\n",
		       saved_root_name);
		while (driver_probe_done() != 0 ||
		       (ROOT_DEV = name_to_dev_t(saved_root_name)) == 0)
			msleep(5);
		/* async_synchronize_full removed - empty stub */
	}

	mount_root();
out:
	init_mount(".", "/", NULL, MS_MOVE, NULL);
	init_chroot(".");
}

/* Merged from noinitramfs.c */
static int __init default_rootfs(void)
{
	return 0;
}
late_initcall(default_rootfs);

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
