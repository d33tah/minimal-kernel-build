#ifndef __SHMEM_FS_H
#define __SHMEM_FS_H
#include <linux/file.h>
#include <linux/swap.h>
#include <linux/mempolicy.h>
#include <linux/pagemap.h>
#include <linux/percpu_counter.h>
#include <linux/xattr.h>
#include <linux/fs_parser.h>
struct shmem_inode_info {
	spinlock_t lock; unsigned int seals; unsigned long flags; unsigned long alloced;
	unsigned long swapped; pgoff_t fallocend; struct list_head shrinklist;
	struct list_head swaplist; struct shared_policy policy; struct simple_xattrs xattrs;
	atomic_t stop_eviction; struct timespec64 i_crtime; struct inode vfs_inode;
};
struct shmem_sb_info {
	unsigned long max_blocks; struct percpu_counter used_blocks; unsigned long max_inodes;
	unsigned long free_inodes; raw_spinlock_t stat_lock; umode_t mode; unsigned char huge;
	kuid_t uid; kgid_t gid; bool full_inums; ino_t next_ino; ino_t __percpu *ino_batch;
	struct mempolicy *mpol; spinlock_t shrinklist_lock; struct list_head shrinklist;
	unsigned long shrinklist_len;
};
extern const struct fs_parameter_spec shmem_fs_parameters[];
extern void shmem_init(void);
/* shmem_init_fs_context removed - declared but never implemented */
extern struct file *shmem_file_setup(const char *name, loff_t size, unsigned long flags);
extern struct file *shmem_kernel_file_setup(const char *name, loff_t size, unsigned long flags);
extern int shmem_zero_setup(struct vm_area_struct *);
extern unsigned long shmem_get_unmapped_area(struct file *, unsigned long addr, unsigned long len, unsigned long pgoff, unsigned long flags);
extern int shmem_lock(struct file *file, int lock, struct ucounts *ucounts);
static inline bool shmem_mapping(struct address_space *mapping) { return false; }
extern void shmem_unlock_mapping(struct address_space *mapping);
extern struct page *shmem_read_mapping_page_gfp(struct address_space *mapping, pgoff_t index, gfp_t gfp_mask);
extern void shmem_truncate_range(struct inode *inode, loff_t start, loff_t end);
int shmem_unuse(unsigned int type);
#endif
