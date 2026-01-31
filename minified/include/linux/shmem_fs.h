#ifndef __SHMEM_FS_H
#define __SHMEM_FS_H
#include <linux/file.h>
#include <linux/swap.h>
/* mempolicy.h removed - forward decl in mm.h */
#include <linux/pagemap.h>
#include <linux/percpu_counter.h>
/* xattr.h removed - not used in minimal kernel */
#include <linux/fs_parser.h>
extern void shmem_init(void);
/* shmem_file_setup, shmem_kernel_file_setup removed - never called */
/* shmem_zero_setup removed - never called */
extern unsigned long shmem_get_unmapped_area(struct file *, unsigned long addr, unsigned long len, unsigned long pgoff, unsigned long flags);
/* shmem_mapping stub removed - never called */
#endif
