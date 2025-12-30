#ifndef __LINUX_BLK_TYPES_H
#define __LINUX_BLK_TYPES_H

#include <linux/types.h>
#include <linux/bvec.h>
#include <linux/device.h>

struct bio;
struct page;
struct cgroup_subsys_state;
/* struct bio_set, bio_integrity_payload, bio_crypt_ctx removed - unused */

/* Minimal block_device struct - only pointers used, never instantiated */
struct block_device {
	int dummy;
};

/* blk_status_t, blk_short_t, blk_qc_t removed - unused */
#define	BLK_STS_OK 0

/* Minimal struct bio - only forward declaration needed */
struct bio {
	int dummy;
};

#endif  
