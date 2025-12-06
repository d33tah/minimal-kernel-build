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

#define PAGE_SECTORS_SHIFT	(PAGE_SHIFT - SECTOR_SHIFT)
#define PAGE_SECTORS		(1 << PAGE_SECTORS_SHIFT)
#define SECTOR_MASK		(PAGE_SECTORS - 1)

struct block_device {
	sector_t		bd_start_sect;
	sector_t		bd_nr_sectors;
	struct disk_stats __percpu *bd_stats;
	unsigned long		bd_stamp;
	bool			bd_read_only;	 
	dev_t			bd_dev;
	atomic_t		bd_openers;
	struct inode *		bd_inode;	 
	struct super_block *	bd_super;
	void *			bd_claiming;
	struct device		bd_device;
	void *			bd_holder;
	int			bd_holders;
	bool			bd_write_holder;
	struct kobject		*bd_holder_dir;
	u8			bd_partno;
	spinlock_t		bd_size_lock;  
	struct gendisk *	bd_disk;
	struct request_queue *	bd_queue;

	 
	int			bd_fsfreeze_count;
	 
	struct mutex		bd_fsfreeze_mutex;
	struct super_block	*bd_fsfreeze_sb;

	struct partition_meta_info *bd_meta_info;
} __randomize_layout;

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
#define BLK_QC_T_NONE		-1U

struct bio {
	struct bio		*bi_next;	 
	struct block_device	*bi_bdev;
	unsigned int		bi_opf;		 
	unsigned short		bi_flags;	 
	unsigned short		bi_ioprio;
	blk_status_t		bi_status;
	atomic_t		__bi_remaining;

	struct bvec_iter	bi_iter;

	blk_qc_t		bi_cookie;
	bio_end_io_t		*bi_end_io;
	void			*bi_private;


	union {
#if defined(CONFIG_BLK_DEV_INTEGRITY)
		struct bio_integrity_payload *bi_integrity;  
#endif
	};

	unsigned short		bi_vcnt;	 

	 

	unsigned short		bi_max_vecs;	 

	atomic_t		__bi_cnt;	 

	struct bio_vec		*bi_io_vec;	 

	struct bio_set		*bi_pool;

	 
	struct bio_vec		bi_inline_vecs[];
};

#define BIO_RESET_BYTES		offsetof(struct bio, bi_max_vecs)
#define BIO_MAX_SECTORS		(UINT_MAX >> SECTOR_SHIFT)

/* Reduced BIO enum - only flags needed for bio.h */
enum {
	BIO_NO_PAGE_REF,
	BIO_CLONED,
	BIO_REFFED,
	BIO_THROTTLED,
	BIO_CHAIN,
	BIO_REMAPPED,
	BIO_FLAG_LAST
};

typedef __u32 __bitwise blk_mq_req_flags_t;

#define REQ_OP_BITS	8
#define REQ_OP_MASK	((1 << REQ_OP_BITS) - 1)
#define REQ_FLAG_BITS	24

enum req_opf {
	REQ_OP_READ		= 0,
	REQ_OP_WRITE		= 1,
	REQ_OP_DISCARD		= 3,
	REQ_OP_SECURE_ERASE	= 5,
	REQ_OP_WRITE_ZEROES	= 9,
	REQ_OP_LAST,
};

enum req_flag_bits {
	__REQ_SYNC = REQ_OP_BITS,
	__REQ_BACKGROUND,
	__REQ_NOWAIT,
	__REQ_CGROUP_PUNT,
	__REQ_POLLED,
	__REQ_ALLOC_CACHE,
	__REQ_NR_BITS,
};

#define REQ_SYNC		(1ULL << __REQ_SYNC)
#define REQ_BACKGROUND		(1ULL << __REQ_BACKGROUND)
#define REQ_NOWAIT		(1ULL << __REQ_NOWAIT)
#define REQ_CGROUP_PUNT		(1ULL << __REQ_CGROUP_PUNT)
#define REQ_POLLED		(1ULL << __REQ_POLLED)
#define REQ_ALLOC_CACHE		(1ULL << __REQ_ALLOC_CACHE)

enum stat_group {
	NR_STAT_GROUPS
};

#define bio_op(bio) \
	((bio)->bi_opf & REQ_OP_MASK)

/* op_is_write removed - unused */

#endif  
