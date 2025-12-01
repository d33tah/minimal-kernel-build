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
	spinlock_t		lock;
	unsigned int		seals;		 
	unsigned long		flags;
	unsigned long		alloced;	 
	unsigned long		swapped;	 
	pgoff_t			fallocend;	 
	struct list_head        shrinklist;      
	struct list_head	swaplist;	 
	struct shared_policy	policy;		 
	struct simple_xattrs	xattrs;		 
	atomic_t		stop_eviction;	 
	struct timespec64	i_crtime;	 
	struct inode		vfs_inode;
};

struct shmem_sb_info {
	unsigned long max_blocks;    
	struct percpu_counter used_blocks;   
	unsigned long max_inodes;    
	unsigned long free_inodes;   
	raw_spinlock_t stat_lock;    
	umode_t mode;		     
	unsigned char huge;	     
	kuid_t uid;		     
	kgid_t gid;		     
	bool full_inums;	     
	ino_t next_ino;		     
	ino_t __percpu *ino_batch;   
	struct mempolicy *mpol;      
	spinlock_t shrinklist_lock;    
	struct list_head shrinklist;   
	unsigned long shrinklist_len;  
};

static inline struct shmem_inode_info *SHMEM_I(struct inode *inode)
{
	return container_of(inode, struct shmem_inode_info, vfs_inode);
}

extern const struct fs_parameter_spec shmem_fs_parameters[];
extern void shmem_init(void);
extern int shmem_init_fs_context(struct fs_context *fc);
extern struct file *shmem_file_setup(const char *name,
					loff_t size, unsigned long flags);
extern struct file *shmem_kernel_file_setup(const char *name, loff_t size,
					    unsigned long flags);
extern struct file *shmem_file_setup_with_mnt(struct vfsmount *mnt,
		const char *name, loff_t size, unsigned long flags);
extern int shmem_zero_setup(struct vm_area_struct *);
extern unsigned long shmem_get_unmapped_area(struct file *, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags);
extern int shmem_lock(struct file *file, int lock, struct ucounts *ucounts);
static inline bool shmem_mapping(struct address_space *mapping)
{
	return false;
}
extern void shmem_unlock_mapping(struct address_space *mapping);
extern struct page *shmem_read_mapping_page_gfp(struct address_space *mapping,
					pgoff_t index, gfp_t gfp_mask);
extern void shmem_truncate_range(struct inode *inode, loff_t start, loff_t end);
int shmem_unuse(unsigned int type);

extern bool shmem_is_huge(struct vm_area_struct *vma,
			  struct inode *inode, pgoff_t index);
static inline bool shmem_huge_enabled(struct vm_area_struct *vma)
{
	return shmem_is_huge(vma, file_inode(vma->vm_file), vma->vm_pgoff);
}
extern unsigned long shmem_swap_usage(struct vm_area_struct *vma);
extern unsigned long shmem_partial_swap_usage(struct address_space *mapping,
						pgoff_t start, pgoff_t end);

enum sgp_type {
	SGP_READ,	 
	SGP_NOALLOC,	 
	SGP_CACHE,	 
	SGP_WRITE,	 
	SGP_FALLOC,	 
};

extern int shmem_getpage(struct inode *inode, pgoff_t index,
		struct page **pagep, enum sgp_type sgp);

static inline struct page *shmem_read_mapping_page(
				struct address_space *mapping, pgoff_t index)
{
	return shmem_read_mapping_page_gfp(mapping, index,
					mapping_gfp_mask(mapping));
}

static inline bool shmem_file(struct file *file)
{
	if (!IS_ENABLED(CONFIG_SHMEM))
		return false;
	if (!file || !file->f_mapping)
		return false;
	return shmem_mapping(file->f_mapping);
}

static inline pgoff_t shmem_fallocend(struct inode *inode, pgoff_t eof)
{
	return max(eof, SHMEM_I(inode)->fallocend);
}

extern bool shmem_charge(struct inode *inode, long pages);
extern void shmem_uncharge(struct inode *inode, long pages);


#endif
