#ifndef __SHMEM_FS_H
#define __SHMEM_FS_H

#include <linux/file.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/fs_parser.h>



extern const struct fs_parameter_spec shmem_fs_parameters[];
extern void shmem_init(void);


#endif
