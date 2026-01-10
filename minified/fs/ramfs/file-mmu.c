

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/ramfs.h>
#include <linux/sched.h>

extern const struct inode_operations ramfs_file_inode_operations;

static unsigned long ramfs_mmu_get_unmapped_area(struct file *file,
						 unsigned long addr,
						 unsigned long len,
						 unsigned long pgoff,
						 unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}

const struct file_operations ramfs_file_operations = {
	.read_iter = generic_file_read_iter,
	.write_iter = generic_file_write_iter,
	.mmap = generic_file_mmap,
	/* fsync removed - fsync syscall returns ENOSYS */
	/* splice_read/write removed - splice syscall returns ENOSYS */
	/* llseek removed - lseek syscall returns ENOSYS */
	.get_unmapped_area = ramfs_mmu_get_unmapped_area,
};

const struct inode_operations ramfs_file_inode_operations = {
	.setattr = simple_setattr,
	.getattr = simple_getattr,
};
