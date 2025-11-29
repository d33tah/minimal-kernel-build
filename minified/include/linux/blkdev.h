 
 
#ifndef _LINUX_BLKDEV_H
#define _LINUX_BLKDEV_H

#include <linux/types.h>
#include <linux/blk_types.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/llist.h>
#include <linux/minmax.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/bio.h>
#include <linux/gfp.h>
#include <linux/kdev_t.h>
#include <linux/rcupdate.h>
#include <linux/percpu-refcount.h>
#include <linux/blkzoned.h>
#include <linux/sched.h>

#include <linux/srcu.h>
#include <linux/uuid.h>
#include <linux/xarray.h>

struct module;
struct request_queue;
struct elevator_queue;
struct blk_trace;
struct request;
struct sg_io_hdr;
struct blkcg_gq;
struct blk_flush_queue;
struct kiocb;
struct pr_ops;
struct rq_qos;
struct blk_queue_stats;
struct blk_stat_callback;
struct blk_crypto_profile;

/* disk_type, part_type, block_class removed - unused */

 
#define BLK_MQ_POLL_STATS_BKTS 16

 
#define BLK_MQ_POLL_CLASSIC -1

 
#define BLKCG_MAX_POLS		6

#define DISK_MAX_PARTS			256
#define DISK_NAME_LEN			32

#define PARTITION_META_INFO_VOLNAMELTH	64
 
struct partition_meta_info;

/* Unused disk/genhd flags - reduced for minimal kernel */
enum { GENHD_FL_LAST };
enum { DISK_EVENT_LAST };
enum { DISK_EVENT_FLAG_LAST };

struct disk_events;
struct badblocks;

struct blk_integrity {
	const struct blk_integrity_profile	*profile;
	unsigned char				flags;
	unsigned char				tuple_size;
	unsigned char				interval_exp;
	unsigned char				tag_size;
};

struct gendisk;
#define GD_NEED_PART_SCAN		0
#define GD_READ_ONLY			1
#define GD_DEAD				2
#define GD_NATIVE_CAPACITY		3
#define GD_ADDED			4
#define GD_SUPPRESS_PART_SCAN		5


 
#define dev_to_disk(device) \
	(dev_to_bdev(device)->bd_disk)
#define disk_to_dev(disk) \
	(&((disk)->part0->bd_device))

#if IS_REACHABLE(CONFIG_CDROM)
#define disk_to_cdi(disk)	((disk)->cdi)
#else
#define disk_to_cdi(disk)	NULL
#endif

static inline dev_t disk_devt(struct gendisk *disk)
{
	return 0;
}

enum blk_zoned_model { BLK_ZONED_NONE = 0 };
enum blk_bounce { BLK_BOUNCE_NONE };

struct queue_limits;

typedef int (*report_zones_cb)(struct blk_zone *zone, unsigned int idx,
			       void *data);

void blk_queue_set_zoned(struct gendisk *disk, enum blk_zoned_model model);


struct blk_independent_access_range;
struct blk_independent_access_ranges;
struct request_queue;

 
#define QUEUE_FLAG_STOPPED	0	 
#define QUEUE_FLAG_DYING	1	 
#define QUEUE_FLAG_HAS_SRCU	2	 
#define QUEUE_FLAG_NOMERGES     3	 
#define QUEUE_FLAG_SAME_COMP	4	 
#define QUEUE_FLAG_FAIL_IO	5	 
#define QUEUE_FLAG_NONROT	6	 
#define QUEUE_FLAG_VIRT		QUEUE_FLAG_NONROT  
#define QUEUE_FLAG_IO_STAT	7	 
#define QUEUE_FLAG_NOXMERGES	9	 
#define QUEUE_FLAG_ADD_RANDOM	10	 
#define QUEUE_FLAG_SAME_FORCE	12	 
#define QUEUE_FLAG_DEAD		13	 
#define QUEUE_FLAG_INIT_DONE	14	 
#define QUEUE_FLAG_STABLE_WRITES 15	 
#define QUEUE_FLAG_POLL		16	 
#define QUEUE_FLAG_WC		17	 
#define QUEUE_FLAG_FUA		18	 
#define QUEUE_FLAG_DAX		19	 
#define QUEUE_FLAG_STATS	20	 
#define QUEUE_FLAG_REGISTERED	22	 
#define QUEUE_FLAG_QUIESCED	24	 
#define QUEUE_FLAG_PCI_P2PDMA	25	 
#define QUEUE_FLAG_ZONE_RESETALL 26	 
#define QUEUE_FLAG_RQ_ALLOC_TIME 27	 
#define QUEUE_FLAG_HCTX_ACTIVE	28	 
#define QUEUE_FLAG_NOWAIT       29	 
#define QUEUE_FLAG_SQ_SCHED     30	 

#define QUEUE_FLAG_MQ_DEFAULT	((1 << QUEUE_FLAG_IO_STAT) |		\
				 (1 << QUEUE_FLAG_SAME_COMP) |		\
				 (1 << QUEUE_FLAG_NOWAIT))

void blk_queue_flag_set(unsigned int flag, struct request_queue *q);
void blk_queue_flag_clear(unsigned int flag, struct request_queue *q);
bool blk_queue_flag_test_and_set(unsigned int flag, struct request_queue *q);

#define blk_queue_stopped(q)	test_bit(QUEUE_FLAG_STOPPED, &(q)->queue_flags)
#define blk_queue_dying(q)	test_bit(QUEUE_FLAG_DYING, &(q)->queue_flags)
#define blk_queue_has_srcu(q)	test_bit(QUEUE_FLAG_HAS_SRCU, &(q)->queue_flags)
#define blk_queue_dead(q)	test_bit(QUEUE_FLAG_DEAD, &(q)->queue_flags)
#define blk_queue_init_done(q)	test_bit(QUEUE_FLAG_INIT_DONE, &(q)->queue_flags)
#define blk_queue_nomerges(q)	test_bit(QUEUE_FLAG_NOMERGES, &(q)->queue_flags)
#define blk_queue_noxmerges(q)	\
	test_bit(QUEUE_FLAG_NOXMERGES, &(q)->queue_flags)
#define blk_queue_nonrot(q)	test_bit(QUEUE_FLAG_NONROT, &(q)->queue_flags)
#define blk_queue_stable_writes(q) \
	test_bit(QUEUE_FLAG_STABLE_WRITES, &(q)->queue_flags)
#define blk_queue_io_stat(q)	test_bit(QUEUE_FLAG_IO_STAT, &(q)->queue_flags)
#define blk_queue_add_random(q)	test_bit(QUEUE_FLAG_ADD_RANDOM, &(q)->queue_flags)
#define blk_queue_zone_resetall(q)	\
	test_bit(QUEUE_FLAG_ZONE_RESETALL, &(q)->queue_flags)
#define blk_queue_dax(q)	test_bit(QUEUE_FLAG_DAX, &(q)->queue_flags)
#define blk_queue_pci_p2pdma(q)	\
	test_bit(QUEUE_FLAG_PCI_P2PDMA, &(q)->queue_flags)
#define blk_queue_rq_alloc_time(q)	false

#define blk_noretry_request(rq) \
	((rq)->cmd_flags & (REQ_FAILFAST_DEV|REQ_FAILFAST_TRANSPORT| \
			     REQ_FAILFAST_DRIVER))
#define blk_queue_quiesced(q)	test_bit(QUEUE_FLAG_QUIESCED, &(q)->queue_flags)
#define blk_queue_pm_only(q)	atomic_read(&(q)->pm_only)
#define blk_queue_registered(q)	test_bit(QUEUE_FLAG_REGISTERED, &(q)->queue_flags)
#define blk_queue_nowait(q)	test_bit(QUEUE_FLAG_NOWAIT, &(q)->queue_flags)
#define blk_queue_sq_sched(q)	test_bit(QUEUE_FLAG_SQ_SCHED, &(q)->queue_flags)


#define list_entry_rq(ptr)	list_entry((ptr), struct request, queuelist)

#define dma_map_bvec(dev, bv, dir, attrs) \
	dma_map_page_attrs(dev, (bv)->bv_page, (bv)->bv_offset, (bv)->bv_len, \
	(dir), (attrs))


#define BLK_DEFAULT_SG_TIMEOUT	(60 * HZ)
#define BLK_MIN_SG_TIMEOUT	(7 * HZ)

int __must_check device_add_disk(struct device *parent, struct gendisk *disk,
				 const struct attribute_group **groups);
void del_gendisk(struct gendisk *gp);
void invalidate_disk(struct gendisk *disk);
void set_disk_ro(struct gendisk *disk, bool read_only);
void disk_uevent(struct gendisk *disk, enum kobject_action action);

bool set_capacity_and_notify(struct gendisk *disk, sector_t size);

struct gendisk *__alloc_disk_node(struct request_queue *q, int node_id,
		struct lock_class_key *lkclass);
void put_disk(struct gendisk *disk);
struct gendisk *__blk_alloc_disk(int node, struct lock_class_key *lkclass);

 
#define blk_alloc_disk(node_id)						\
({									\
	static struct lock_class_key __key;				\
									\
	__blk_alloc_disk(node_id, &__key);				\
})

int __register_blkdev(unsigned int major, const char *name,
		void (*probe)(dev_t devt));
#define register_blkdev(major, name) \
	__register_blkdev(major, name, NULL)
void unregister_blkdev(unsigned int major, const char *name);

int __invalidate_device(struct block_device *bdev, bool kill_dirty);
void set_capacity(struct gendisk *disk, sector_t size);

dev_t part_devt(struct gendisk *disk, u8 partno);
void inc_diskseq(struct gendisk *disk);
dev_t blk_lookup_devt(const char *name, int partno);
void blk_request_module(dev_t devt);

void submit_bio_noacct(struct bio *bio);

int blk_status_to_errno(blk_status_t status);
blk_status_t errno_to_blk_status(int errno);

/* BLK_POLL defines removed - unused in minimal kernel */
int bio_poll(struct bio *bio, struct io_comp_batch *iob, unsigned int flags);
int iocb_bio_iopoll(struct kiocb *kiocb, struct io_comp_batch *iob,
			unsigned int flags);


bool __must_check blk_get_queue(struct request_queue *);
extern void blk_put_queue(struct request_queue *);

void blk_mark_disk_dead(struct gendisk *disk);

struct blk_plug {
};

static inline void blk_start_plug(struct blk_plug *plug)
{
}

static inline void blk_finish_plug(struct blk_plug *plug)
{
}

static inline void blk_flush_plug(struct blk_plug *plug, bool async)
{
}

static inline int blkdev_issue_flush(struct block_device *bdev)
{
	return 0;
}

static inline long nr_blockdev_pages(void)
{
	return 0;
}

extern void blk_io_schedule(void);

int blkdev_issue_discard(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask);
int __blkdev_issue_discard(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, struct bio **biop);
int blkdev_issue_secure_erase(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp);

#define BLKDEV_ZERO_NOUNMAP	(1 << 0)   
#define BLKDEV_ZERO_NOFALLBACK	(1 << 1)   

/* __blkdev_issue_zeroout, blkdev_issue_zeroout removed - unused */

enum blk_default_limits { BLK_DEFAULT_LIMITS_LAST };

int bdev_alignment_offset(struct block_device *bdev);
unsigned int bdev_discard_alignment(struct block_device *bdev);

 
int kblockd_schedule_work(struct work_struct *work);
int kblockd_mod_delayed_work_on(int cpu, struct delayed_work *dwork, unsigned long delay);

#define MODULE_ALIAS_BLOCKDEV(major,minor) \
	MODULE_ALIAS("block-major-" __stringify(major) "-" __stringify(minor))
#define MODULE_ALIAS_BLOCKDEV_MAJOR(major) \
	MODULE_ALIAS("block-major-" __stringify(major) "-*")


enum blk_unique_id { BLK_UID_LAST };

#define NFL4_UFLG_MASK			0x0000003F

struct block_device_operations;

#define blkdev_compat_ptr_ioctl NULL

int bdev_read_only(struct block_device *bdev);
int set_blocksize(struct block_device *bdev, int size);

const char *bdevname(struct block_device *bdev, char *buffer);
int lookup_bdev(const char *pathname, dev_t *dev);

#define BDEVNAME_SIZE	32	 
#define BDEVT_SIZE	10	 
#define BLKDEV_MAJOR_MAX	0

struct block_device *blkdev_get_by_path(const char *path, fmode_t mode,
		void *holder);
struct block_device *blkdev_get_by_dev(dev_t dev, fmode_t mode, void *holder);
int bd_prepare_to_claim(struct block_device *bdev, void *holder);
void bd_abort_claiming(struct block_device *bdev, void *holder);
void blkdev_put(struct block_device *bdev, fmode_t mode);

 
struct block_device *blkdev_get_no_open(dev_t dev);
void blkdev_put_no_open(struct block_device *bdev);

struct block_device *bdev_alloc(struct gendisk *disk, u8 partno);
void bdev_add(struct block_device *bdev, dev_t dev);
struct block_device *I_BDEV(struct inode *inode);
int truncate_bdev_range(struct block_device *bdev, fmode_t mode, loff_t lstart,
		loff_t lend);

static inline void invalidate_bdev(struct block_device *bdev)
{
}
static inline void printk_all_partitions(void)
{
}

int fsync_bdev(struct block_device *bdev);

int freeze_bdev(struct block_device *bdev);
int thaw_bdev(struct block_device *bdev);

struct io_comp_batch;

#endif  
