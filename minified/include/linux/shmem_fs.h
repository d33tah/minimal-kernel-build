#ifndef __SHMEM_FS_H
#define __SHMEM_FS_H

#include <linux/file.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/percpu_counter.h>
#include <linux/xattr.h>
#include <linux/fs_parser.h>



extern const struct fs_parameter_spec shmem_fs_parameters[];
extern void shmem_init(void);
extern unsigned long shmem_get_unmapped_area(struct file *, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags);
static inline bool shmem_mapping(struct address_space *mapping)
{
	return false;
}
extern struct page *shmem_read_mapping_page_gfp(struct address_space *mapping,
					pgoff_t index, gfp_t gfp_mask);



#endif
