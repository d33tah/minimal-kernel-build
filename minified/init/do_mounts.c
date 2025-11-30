 
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
#include <linux/shmem_fs.h>

 
 
 
#include <uapi/linux/mount.h>

#include "do_mounts.h"

int root_mountflags = MS_RDONLY | MS_SILENT;
static char * __initdata root_device_name;
static char __initdata saved_root_name[64];
static int root_wait;

dev_t ROOT_DEV;

/* Stub: load_ramdisk deprecated, not needed for minimal kernel */
static int __init load_ramdisk(char *str) { return 1; }
__setup("load_ramdisk=", load_ramdisk);

/* Stub: ro/rw cmdline not needed for minimal kernel */
static int __init readonly(char *str) { return 1; }
static int __init readwrite(char *str) { return 1; }
__setup("ro", readonly);
__setup("rw", readwrite);


static dev_t devt_from_devnum(const char *name)
{
	unsigned maj, min, offset;
	dev_t devt = 0;
	char *p, dummy;

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

 
dev_t name_to_dev_t(const char *name)
{
	if (strcmp(name, "/dev/nfs") == 0)
		return Root_NFS;
	if (strcmp(name, "/dev/cifs") == 0)
		return Root_CIFS;
	if (strcmp(name, "/dev/ram") == 0)
		return Root_RAM0;
	return devt_from_devnum(name);
}

/* Stub: root= cmdline not needed for minimal kernel */
static int __init root_dev_setup(char *line) { return 1; }
__setup("root=", root_dev_setup);

/* Stub: rootwait cmdline not needed for minimal kernel */
static int __init rootwait_setup(char *str) { return 1; }
__setup("rootwait", rootwait_setup);

static char * __initdata root_mount_data;
/* Stub: rootflags= not needed for minimal kernel */
static int __init root_data_setup(char *str) { return 1; }

static char * __initdata root_fs_names;
/* Stub: rootfstype= not needed for minimal kernel */
static int __init fs_names_setup(char *str) { return 1; }

static unsigned int __initdata root_delay;
/* Stub: rootdelay= not needed for minimal kernel */
static int __init root_delay_setup(char *str) { return 1; }

__setup("rootflags=", root_data_setup);
__setup("rootfstype=", fs_names_setup);
__setup("rootdelay=", root_delay_setup);

 
static int __init split_fs_names(char *page, size_t size, char *names)
{
	int count = 1;
	char *p = page;

	strlcpy(p, root_fs_names, size);
	while (*p++) {
		if (p[-1] == ',') {
			p[-1] = '\0';
			count++;
		}
	}

	return count;
}

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
	       s->s_type->name,
	       sb_rdonly(s) ? " readonly" : "",
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

	scnprintf(b, BDEVNAME_SIZE, "unknown-block(%u,%u)",
		  MAJOR(ROOT_DEV), MINOR(ROOT_DEV));
	if (root_fs_names)
		num_fs = split_fs_names(fs_names, PAGE_SIZE, root_fs_names);
	else
		num_fs = list_bdev_fs_names(fs_names, PAGE_SIZE);
retry:
	for (i = 0, p = fs_names; i < num_fs; i++, p += strlen(p)+1) {
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
	for (i = 0, p = fs_names; i < num_fs; i++, p += strlen(p)+1)
		printk(" %s", p);
	printk("\n");
	panic("VFS: Unable to mount root fs on %s", b);
out:
	put_page(page);
}
 


static bool __init fs_is_nodev(char *fstype)
{
	struct file_system_type *fs = get_fs_type(fstype);
	bool ret = false;

	if (fs) {
		ret = !(fs->fs_flags & FS_REQUIRES_DEV);
		put_filesystem(fs);
	}

	return ret;
}

static int __init mount_nodev_root(void)
{
	char *fs_names, *fstype;
	int err = -EINVAL;
	int num_fs, i;

	fs_names = (void *)__get_free_page(GFP_KERNEL);
	if (!fs_names)
		return -EINVAL;
	num_fs = split_fs_names(fs_names, PAGE_SIZE, root_fs_names);

	for (i = 0, fstype = fs_names; i < num_fs;
	     i++, fstype += strlen(fstype) + 1) {
		if (!*fstype)
			continue;
		if (!fs_is_nodev(fstype))
			continue;
		err = do_mount_root(root_device_name, fstype, root_mountflags,
				    root_mount_data);
		if (!err)
			break;
	}

	free_page((unsigned long)fs_names);
	return err;
}

void __init mount_root(void)
{
	if (ROOT_DEV == 0 && root_device_name && root_fs_names) {
		if (mount_nodev_root() == 0)
			return;
	}
}

 
void __init prepare_namespace(void)
{
	if (root_delay) {
		printk(KERN_INFO "Waiting %d sec before mounting root device...\n",
		       root_delay);
		ssleep(root_delay);
	}

	 
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

	if (initrd_load())
		goto out;

	 
	if ((ROOT_DEV == 0) && root_wait) {
		printk(KERN_INFO "Waiting for root device %s...\n",
			saved_root_name);
		while (driver_probe_done() != 0 ||
			(ROOT_DEV = name_to_dev_t(saved_root_name)) == 0)
			msleep(5);
		async_synchronize_full();
	}

	mount_root();
out:
	devtmpfs_mount();
	init_mount(".", "/", NULL, MS_MOVE, NULL);
	init_chroot(".");
}

static bool is_tmpfs;
static int rootfs_init_fs_context(struct fs_context *fc)
{
	if (IS_ENABLED(CONFIG_TMPFS) && is_tmpfs)
		return shmem_init_fs_context(fc);

	return ramfs_init_fs_context(fc);
}

struct file_system_type rootfs_fs_type = {
	.name		= "rootfs",
	.init_fs_context = rootfs_init_fs_context,
	.kill_sb	= kill_litter_super,
};

void __init init_rootfs(void)
{
	if (IS_ENABLED(CONFIG_TMPFS) && !saved_root_name[0] &&
		(!root_fs_names || strstr(root_fs_names, "tmpfs")))
		is_tmpfs = true;
}
