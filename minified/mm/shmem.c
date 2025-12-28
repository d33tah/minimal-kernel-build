
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/statfs.h> /* vfs.h redirect */
#include <linux/mount.h>
#include <linux/ramfs.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/export.h>
#include <linux/swap.h>
#include <linux/uio.h>
#include <linux/hugetlb.h>
#include <linux/fs_parser.h>
struct swap_iocb;

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
#define shmem_acct_size(flags, size) 0
#define shmem_unacct_size(flags, size) \
	do {                           \
	} while (0)

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

	if (shmem_acct_size(flags, size))
		return ERR_PTR(-ENOMEM);

	inode = shmem_get_inode(mnt->mnt_sb, NULL, S_IFREG | S_IRWXUGO, 0,
				flags);
	if (unlikely(!inode)) {
		shmem_unacct_size(flags, size);
		return ERR_PTR(-ENOSPC);
	}
	inode->i_flags |= i_flags;
	inode->i_size = size;
	clear_nlink(inode);
	res = ERR_PTR(ramfs_nommu_expand_for_mapping(inode, size));
	if (!IS_ERR(res))
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

struct file *shmem_file_setup(const char *name, loff_t size,
			      unsigned long flags)
{
	return __shmem_file_setup(shm_mnt, name, size, flags, 0);
}

int shmem_zero_setup(struct vm_area_struct *vma)
{
	struct file *file;
	loff_t size = vma->vm_end - vma->vm_start;

	file = shmem_kernel_file_setup("dev/zero", size, vma->vm_flags);
	if (IS_ERR(file))
		return PTR_ERR(file);

	if (vma->vm_file)
		fput(vma->vm_file);
	vma->vm_file = file;
	vma->vm_ops = &shmem_vm_ops;

	return 0;
}
