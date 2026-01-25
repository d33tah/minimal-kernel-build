#ifndef __LINUX_BVEC_H
#define __LINUX_BVEC_H

#include <linux/highmem.h>
#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/limits.h>
#include <linux/minmax.h>
#include <linux/mm.h>
#include <linux/types.h>

struct page;

struct bio_vec {
	struct page	*bv_page;
	unsigned int	bv_len;
	unsigned int	bv_offset;
};

struct bvec_iter {
	sector_t		bi_sector;	 
	unsigned int		bi_size;	 

	unsigned int		bi_idx;		 

	unsigned int            bi_bvec_done;	 
} __packed;


/* bvec_iter_advance inlined into iov_iter.c */


#endif  
