#ifndef __SHMEM_FS_H
#define __SHMEM_FS_H
#include <linux/file.h>
#include <linux/swap.h>
#include <linux/mempolicy.h>
#include <linux/pagemap.h>
#include <linux/percpu_counter.h>
#include <linux/xattr.h>
#include <linux/fs_parser.h>
extern void shmem_init(void);
extern struct file *shmem_file_setup(const char *name, loff_t size, unsigned long flags);
extern struct file *shmem_kernel_file_setup(const char *name, loff_t size, unsigned long flags);
extern int shmem_zero_setup(struct vm_area_struct *);
extern unsigned long shmem_get_unmapped_area(struct file *, unsigned long addr, unsigned long len, unsigned long pgoff, unsigned long flags);
/* shmem_mapping stub removed - never called */
#endif
