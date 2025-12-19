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

#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT 9
#endif
#ifndef SECTOR_SIZE
#define SECTOR_SIZE (1 << SECTOR_SHIFT)
#endif


/* Minimal block_device struct - only pointers used, never instantiated */
struct block_device {
	int dummy;
};

#define bdev_whole(_bdev) \
	((_bdev)->bd_disk->part0)

#define dev_to_bdev(device) \
	container_of((device), struct block_device, bd_device)

#define bdev_kobj(_bdev) \
	(&((_bdev)->bd_device.kobj))

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

/* REQ flags used by writeback.h */
#define REQ_SYNC		(1ULL << 8)
#define REQ_BACKGROUND		(1ULL << 9)
#define REQ_CGROUP_PUNT		(1ULL << 11)


#endif  
