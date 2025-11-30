 
 
#ifndef WRITEBACK_H
#define WRITEBACK_H

#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/flex_proportions.h>
#include <linux/backing-dev-defs.h>
#include <linux/blk_types.h>

struct bio;

DECLARE_PER_CPU(int, dirty_throttle_leaks);

 
#define DIRTY_SCOPE		8
#define DIRTY_FULL_SCOPE	(DIRTY_SCOPE / 2)

struct backing_dev_info;

 
enum writeback_sync_modes {
	WB_SYNC_NONE,	 
	WB_SYNC_ALL,	 
};

 
struct writeback_control {
	long nr_to_write;		 
	long pages_skipped;		 

	 
	loff_t range_start;
	loff_t range_end;

	enum writeback_sync_modes sync_mode;

	unsigned for_kupdate:1;		 
	unsigned for_background:1;	 
	unsigned tagged_writepages:1;	 
	unsigned for_reclaim:1;		 
	unsigned range_cyclic:1;	 
	unsigned for_sync:1;		 
	unsigned unpinned_fscache_wb:1;	 

	 
	unsigned no_cgroup_owner:1;

	unsigned punt_to_cgroup:1;	 

	 
	struct swap_iocb **swap_plug;

};

static inline int wbc_to_write_flags(struct writeback_control *wbc)
{
	int flags = 0;

	if (wbc->punt_to_cgroup)
		flags = REQ_CGROUP_PUNT;

	if (wbc->sync_mode == WB_SYNC_ALL)
		flags |= REQ_SYNC;
	else if (wbc->for_kupdate || wbc->for_background)
		flags |= REQ_BACKGROUND;

	return flags;
}

#define wbc_blkcg_css(wbc)		(blkcg_root_css)

 
struct wb_domain {
	spinlock_t lock;

	 
	struct fprop_global completions;
	struct timer_list period_timer;	 
	unsigned long period_time;

	 
	unsigned long dirty_limit_tstamp;
	unsigned long dirty_limit;
};

 
static inline void wb_domain_size_changed(struct wb_domain *dom)
{
	spin_lock(&dom->lock);
	dom->dirty_limit_tstamp = jiffies;
	dom->dirty_limit = 0;
	spin_unlock(&dom->lock);
}

 	
struct bdi_writeback;
void writeback_inodes_sb(struct super_block *, enum wb_reason reason);
void writeback_inodes_sb_nr(struct super_block *, unsigned long nr,
							enum wb_reason reason);
void try_to_writeback_inodes_sb(struct super_block *sb, enum wb_reason reason);
void sync_inodes_sb(struct super_block *);
void wakeup_flusher_threads(enum wb_reason reason);
void wakeup_flusher_threads_bdi(struct backing_dev_info *bdi,
				enum wb_reason reason);
void inode_wait_for_writeback(struct inode *inode);
void inode_io_list_del(struct inode *inode);

 
static inline void wait_on_inode(struct inode *inode)
{
	might_sleep();
	wait_on_bit(&inode->i_state, __I_NEW, TASK_UNINTERRUPTIBLE);
}


static inline void inode_attach_wb(struct inode *inode, struct page *page)
{
}

static inline void inode_detach_wb(struct inode *inode)
{
}

static inline void wbc_attach_and_unlock_inode(struct writeback_control *wbc,
					       struct inode *inode)
	__releases(&inode->i_lock)
{
	spin_unlock(&inode->i_lock);
}

static inline void wbc_attach_fdatawrite_inode(struct writeback_control *wbc,
					       struct inode *inode)
{
}

static inline void wbc_detach_inode(struct writeback_control *wbc)
{
}

static inline void wbc_init_bio(struct writeback_control *wbc, struct bio *bio)
{
}

static inline void wbc_account_cgroup_owner(struct writeback_control *wbc,
					    struct page *page, size_t bytes)
{
}

static inline void cgroup_writeback_umount(void)
{
}


extern struct wb_domain global_wb_domain;

void laptop_mode_timer_fn(struct timer_list *t);

extern unsigned int dirty_writeback_interval;
extern unsigned int dirty_expire_interval;
extern unsigned int dirtytime_expire_interval;
extern int laptop_mode;

int dirtytime_interval_handler(struct ctl_table *table, int write,
		void *buffer, size_t *lenp, loff_t *ppos);

void wb_update_bandwidth(struct bdi_writeback *wb);
void balance_dirty_pages_ratelimited(struct address_space *mapping);

typedef int (*writepage_t)(struct page *page, struct writeback_control *wbc,
				void *data);

int do_writepages(struct address_space *mapping, struct writeback_control *wbc);

bool filemap_dirty_folio(struct address_space *mapping, struct folio *folio);
void folio_account_redirty(struct folio *folio);
static inline void account_page_redirty(struct page *page)
{
	folio_account_redirty(page_folio(page));
}
bool folio_redirty_for_writepage(struct writeback_control *, struct folio *);
bool redirty_page_for_writepage(struct writeback_control *, struct page *);

void sb_mark_inode_writeback(struct inode *inode);
void sb_clear_inode_writeback(struct inode *inode);



#endif		 
