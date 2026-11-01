#include <linux/fs.h>
#include <linux/ramfs.h>

struct file_system_type rootfs_fs_type = {
	.init_fs_context = ramfs_init_fs_context,
};
