 
 
#ifndef __LINUX_BLK_TYPES_H
#define __LINUX_BLK_TYPES_H

#include <linux/types.h>
#include <linux/bvec.h>
#include <linux/device.h>
#include <linux/ktime.h>

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

 
#if defined(CONFIG_ALPHA) && !defined(__alpha_bwx__)
typedef u32 __bitwise blk_status_t;
typedef u32 blk_short_t;
#else
typedef u8 __bitwise blk_status_t;
typedef u16 blk_short_t;
#endif
#define	BLK_STS_OK 0
#define BLK_STS_NOTSUPP		((__force blk_status_t)1)
#define BLK_STS_TIMEOUT		((__force blk_status_t)2)
#define BLK_STS_NOSPC		((__force blk_status_t)3)
#define BLK_STS_TRANSPORT	((__force blk_status_t)4)
#define BLK_STS_TARGET		((__force blk_status_t)5)
#define BLK_STS_NEXUS		((__force blk_status_t)6)
#define BLK_STS_MEDIUM		((__force blk_status_t)7)
#define BLK_STS_PROTECTION	((__force blk_status_t)8)
#define BLK_STS_RESOURCE	((__force blk_status_t)9)
#define BLK_STS_IOERR		((__force blk_status_t)10)

 
#define BLK_STS_DM_REQUEUE    ((__force blk_status_t)11)

 
#define BLK_STS_AGAIN		((__force blk_status_t)12)

 
#define BLK_STS_DEV_RESOURCE	((__force blk_status_t)13)

 
#define BLK_STS_ZONE_RESOURCE	((__force blk_status_t)14)

 
#define BLK_STS_ZONE_OPEN_RESOURCE	((__force blk_status_t)15)

 
#define BLK_STS_ZONE_ACTIVE_RESOURCE	((__force blk_status_t)16)

 
#define BLK_STS_OFFLINE		((__force blk_status_t)17)

 
static inline bool blk_path_error(blk_status_t error)
{
	switch (error) {
	case BLK_STS_NOTSUPP:
	case BLK_STS_NOSPC:
	case BLK_STS_TARGET:
	case BLK_STS_NEXUS:
	case BLK_STS_MEDIUM:
	case BLK_STS_PROTECTION:
		return false;
	}

	 
	return true;
}

 
#define BIO_ISSUE_RES_BITS      1
#define BIO_ISSUE_SIZE_BITS     12
#define BIO_ISSUE_RES_SHIFT     (64 - BIO_ISSUE_RES_BITS)
#define BIO_ISSUE_SIZE_SHIFT    (BIO_ISSUE_RES_SHIFT - BIO_ISSUE_SIZE_BITS)
#define BIO_ISSUE_TIME_MASK     ((1ULL << BIO_ISSUE_SIZE_SHIFT) - 1)
#define BIO_ISSUE_SIZE_MASK     \
	(((1ULL << BIO_ISSUE_SIZE_BITS) - 1) << BIO_ISSUE_SIZE_SHIFT)
#define BIO_ISSUE_RES_MASK      (~((1ULL << BIO_ISSUE_RES_SHIFT) - 1))

 
#define BIO_ISSUE_THROTL_SKIP_LATENCY (1ULL << 63)

struct bio_issue {
	u64 value;
};

static inline u64 __bio_issue_time(u64 time)
{
	return time & BIO_ISSUE_TIME_MASK;
}

static inline u64 bio_issue_time(struct bio_issue *issue)
{
	return __bio_issue_time(issue->value);
}

static inline sector_t bio_issue_size(struct bio_issue *issue)
{
	return ((issue->value & BIO_ISSUE_SIZE_MASK) >> BIO_ISSUE_SIZE_SHIFT);
}

static inline void bio_issue_init(struct bio_issue *issue,
				       sector_t size)
{
	size &= (1ULL << BIO_ISSUE_SIZE_BITS) - 1;
	issue->value = ((issue->value & BIO_ISSUE_RES_MASK) |
			(ktime_get_ns() & BIO_ISSUE_TIME_MASK) |
			((u64)size << BIO_ISSUE_SIZE_SHIFT));
}

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

 
enum {
	BIO_NO_PAGE_REF,	 
	BIO_CLONED,		 
	BIO_BOUNCED,		 
	BIO_WORKINGSET,		 
	BIO_QUIET,		 
	BIO_CHAIN,		 
	BIO_REFFED,		 
	BIO_THROTTLED,		 
	BIO_TRACE_COMPLETION,	 
	BIO_CGROUP_ACCT,	 
	BIO_QOS_THROTTLED,	 
	BIO_QOS_MERGED,		 
	BIO_REMAPPED,
	BIO_ZONE_WRITE_LOCKED,	 
	BIO_FLAG_LAST
};

typedef __u32 __bitwise blk_mq_req_flags_t;

 
#define REQ_OP_BITS	8
#define REQ_OP_MASK	((1 << REQ_OP_BITS) - 1)
#define REQ_FLAG_BITS	24

enum req_opf {
	 
	REQ_OP_READ		= 0,
	 
	REQ_OP_WRITE		= 1,
	 
	REQ_OP_FLUSH		= 2,
	 
	REQ_OP_DISCARD		= 3,
	 
	REQ_OP_SECURE_ERASE	= 5,
	 
	REQ_OP_WRITE_ZEROES	= 9,
	 
	REQ_OP_ZONE_OPEN	= 10,
	 
	REQ_OP_ZONE_CLOSE	= 11,
	 
	REQ_OP_ZONE_FINISH	= 12,
	 
	REQ_OP_ZONE_APPEND	= 13,
	 
	REQ_OP_ZONE_RESET	= 15,
	 
	REQ_OP_ZONE_RESET_ALL	= 17,

	 
	REQ_OP_DRV_IN		= 34,
	REQ_OP_DRV_OUT		= 35,

	REQ_OP_LAST,
};

enum req_flag_bits {
	__REQ_FAILFAST_DEV =	 
		REQ_OP_BITS,
	__REQ_FAILFAST_TRANSPORT,  
	__REQ_FAILFAST_DRIVER,	 
	__REQ_SYNC,		 
	__REQ_META,		 
	__REQ_PRIO,		 
	__REQ_NOMERGE,		 
	__REQ_IDLE,		 
	__REQ_INTEGRITY,	 
	__REQ_FUA,		 
	__REQ_PREFLUSH,		 
	__REQ_RAHEAD,		 
	__REQ_BACKGROUND,	 
	__REQ_NOWAIT,            
	 
	__REQ_CGROUP_PUNT,
	__REQ_POLLED,		 
	__REQ_ALLOC_CACHE,	 
	__REQ_SWAP,		 
	__REQ_DRV,		 

	 
	 
	__REQ_NOUNMAP,		 

	__REQ_NR_BITS,		 
};

#define REQ_FAILFAST_DEV	(1ULL << __REQ_FAILFAST_DEV)
#define REQ_FAILFAST_TRANSPORT	(1ULL << __REQ_FAILFAST_TRANSPORT)
#define REQ_FAILFAST_DRIVER	(1ULL << __REQ_FAILFAST_DRIVER)
#define REQ_SYNC		(1ULL << __REQ_SYNC)
#define REQ_META		(1ULL << __REQ_META)
#define REQ_PRIO		(1ULL << __REQ_PRIO)
#define REQ_NOMERGE		(1ULL << __REQ_NOMERGE)
#define REQ_IDLE		(1ULL << __REQ_IDLE)
#define REQ_INTEGRITY		(1ULL << __REQ_INTEGRITY)
#define REQ_FUA			(1ULL << __REQ_FUA)
#define REQ_PREFLUSH		(1ULL << __REQ_PREFLUSH)
#define REQ_RAHEAD		(1ULL << __REQ_RAHEAD)
#define REQ_BACKGROUND		(1ULL << __REQ_BACKGROUND)
#define REQ_NOWAIT		(1ULL << __REQ_NOWAIT)
#define REQ_CGROUP_PUNT		(1ULL << __REQ_CGROUP_PUNT)

#define REQ_NOUNMAP		(1ULL << __REQ_NOUNMAP)
#define REQ_POLLED		(1ULL << __REQ_POLLED)
#define REQ_ALLOC_CACHE		(1ULL << __REQ_ALLOC_CACHE)

#define REQ_DRV			(1ULL << __REQ_DRV)
#define REQ_SWAP		(1ULL << __REQ_SWAP)

#define REQ_FAILFAST_MASK \
	(REQ_FAILFAST_DEV | REQ_FAILFAST_TRANSPORT | REQ_FAILFAST_DRIVER)

#define REQ_NOMERGE_FLAGS \
	(REQ_NOMERGE | REQ_PREFLUSH | REQ_FUA)

enum stat_group {
	STAT_READ,
	STAT_WRITE,
	STAT_DISCARD,
	STAT_FLUSH,

	NR_STAT_GROUPS
};

#define bio_op(bio) \
	((bio)->bi_opf & REQ_OP_MASK)


static inline bool op_is_write(unsigned int op)
{
	return (op & 1);
}


struct blk_rq_stat {
	u64 mean;
	u64 min;
	u64 max;
	u32 nr_samples;
	u64 batch;
};

#endif  
