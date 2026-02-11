
#include <linux/fs.h>
#include <linux/init.h>
#include <asm/statfs.h>
#include <asm/byteorder.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/swap.h>
#include <linux/uio.h>
/* hugetlb.h removed - unused */
#include <linux/fs_parser.h>
/* Inlined from ramfs.h */
extern int ramfs_init_fs_context(struct fs_context *fc);
extern const struct fs_parameter_spec ramfs_fs_parameters[];
extern const struct file_operations ramfs_file_operations;
extern const struct vm_operations_struct generic_file_vm_ops;
/* struct swap_iocb forward decl removed - unused */

static struct vfsmount *shm_mnt;

static struct file_system_type shmem_fs_type = {
	.name = "tmpfs",
	.init_fs_context = ramfs_init_fs_context,
	.parameters = ramfs_fs_parameters,
	.kill_sb = kill_litter_super,
	/* .fs_flags removed - write-only field */
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

/* shmem_vm_ops, shmem_file_operations, shmem_get_inode removed - never used */
/* shmem_acct_size, shmem_unacct_size removed - always returned 0 / did nothing */
/* __shmem_file_setup, shmem_kernel_file_setup, shmem_file_setup removed - never called */
/* shmem_zero_setup removed - never called */
