#ifndef __LINUX_BLK_TYPES_H
#define __LINUX_BLK_TYPES_H

#include <linux/types.h>
#include <linux/bvec.h>
#include <linux/device.h>

struct bio_set;
struct bio;
struct bio_integrity_payload;
struct page;
struct io_context;
struct cgroup_subsys_state;
typedef void (bio_end_io_t) (struct bio *);
struct bio_crypt_ctx;

/* Minimal block_device struct - only pointers used, never instantiated */
struct block_device {
	int dummy;
};

/* Reduced blk_status_t - only BLK_STS_OK needed in minimal kernel */
#if defined(CONFIG_ALPHA) && !defined(__alpha_bwx__)
typedef u32 __bitwise blk_status_t;
typedef u32 blk_short_t;
#else
typedef u8 __bitwise blk_status_t;
typedef u16 blk_short_t;
#endif
#define	BLK_STS_OK 0

typedef unsigned int blk_qc_t;

/* Minimal struct bio - only forward declaration needed */
struct bio {
	int dummy;
};

#endif  
