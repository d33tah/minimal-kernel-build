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

struct wb_domain {
	spinlock_t lock;

	 
	struct fprop_global completions;
	struct timer_list period_timer;	 
	unsigned long period_time;

	 
	unsigned long dirty_limit_tstamp;
	unsigned long dirty_limit;
};


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

/* wait_on_inode removed - never called */

static inline void inode_detach_wb(struct inode *inode)
{
}

static inline void wbc_attach_fdatawrite_inode(struct writeback_control *wbc,
					       struct inode *inode)
{
}

static inline void wbc_detach_inode(struct writeback_control *wbc)
{
}

static inline void cgroup_writeback_umount(void)
{
}


void laptop_mode_timer_fn(struct timer_list *t);

void wb_update_bandwidth(struct bdi_writeback *wb);
void balance_dirty_pages_ratelimited(struct address_space *mapping);

typedef int (*writepage_t)(struct page *page, struct writeback_control *wbc,
				void *data);

int do_writepages(struct address_space *mapping, struct writeback_control *wbc);

bool filemap_dirty_folio(struct address_space *mapping, struct folio *folio);
/* folio_account_redirty and account_page_redirty removed - unused */
bool folio_redirty_for_writepage(struct writeback_control *, struct folio *);

/* sb_mark_inode_writeback, sb_clear_inode_writeback removed - unused */



#endif		 
