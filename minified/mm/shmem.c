
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/types.h>
#include <asm/statfs.h>
#include <asm/byteorder.h>
#include <linux/mount.h>
#include <linux/ramfs.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/swap.h>
#include <linux/uio.h>
/* hugetlb.h removed - unused */
#include <linux/fs_parser.h>
/* struct swap_iocb forward decl removed - unused */

static struct vfsmount *shm_mnt;

static struct file_system_type shmem_fs_type = {
	.name = "tmpfs",
	.init_fs_context = ramfs_init_fs_context,
	.parameters = ramfs_fs_parameters,
	.kill_sb = kill_litter_super,
	.fs_flags = FS_USERNS_MOUNT,
};

void __init shmem_init(void)
{
	BUG_ON(register_filesystem(&shmem_fs_type) != 0);

	shm_mnt = kern_mount(&shmem_fs_type);
	BUG_ON(IS_ERR(shm_mnt));
}

unsigned long shmem_get_unmapped_area(struct file *file, unsigned long addr,
				      unsigned long len, unsigned long pgoff,
				      unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}

#define shmem_vm_ops generic_file_vm_ops
#define shmem_file_operations ramfs_file_operations
#define shmem_get_inode(sb, dir, mode, dev, flags) \
	ramfs_get_inode(sb, dir, mode, dev)
/* shmem_acct_size, shmem_unacct_size removed - always returned 0 / did nothing */

static struct file *__shmem_file_setup(struct vfsmount *mnt, const char *name,
				       loff_t size, unsigned long flags,
				       unsigned int i_flags)
{
	struct inode *inode;
	struct file *res;

	if (IS_ERR(mnt))
		return ERR_CAST(mnt);

	if (size < 0 || size > MAX_LFS_FILESIZE)
		return ERR_PTR(-EINVAL);

	inode = shmem_get_inode(mnt->mnt_sb, NULL, S_IFREG | S_IRWXUGO, 0,
				flags);
	if (unlikely(!inode))
		return ERR_PTR(-ENOSPC);
	inode->i_flags |= i_flags;
	inode->i_size = size;
	clear_nlink(inode);
	res = alloc_file_pseudo(inode, mnt, name, O_RDWR,
				&shmem_file_operations);
	if (IS_ERR(res))
		iput(inode);
	return res;
}

struct file *shmem_kernel_file_setup(const char *name, loff_t size,
				     unsigned long flags)
{
	return __shmem_file_setup(shm_mnt, name, size, flags, S_PRIVATE);
}

/* shmem_file_setup removed - never called */
/* shmem_zero_setup removed - never called */
