
#include <linux/fs.h>
#include <linux/ramfs.h>

static struct vfsmount *shm_mnt;



static struct file_system_type shmem_fs_type = { .name		= "tmpfs", .init_fs_context = ramfs_init_fs_context, .kill_sb	= kill_litter_super, .fs_flags	= FS_USERNS_MOUNT, };

void __init shmem_init(void) {
	BUG_ON(register_filesystem(&shmem_fs_type) != 0);

	shm_mnt = kern_mount(&shmem_fs_type);
	BUG_ON(IS_ERR(shm_mnt)); }

