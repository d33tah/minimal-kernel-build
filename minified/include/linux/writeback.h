#ifndef WRITEBACK_H
#define WRITEBACK_H

/* linux/sched.h, linux/workqueue.h removed - unused */
#include <linux/fs.h>
#include <linux/flex_proportions.h>
#include <linux/backing-dev-defs.h>

/* struct bio forward decl removed - already in blk_types.h */

/* dirty_throttle_leaks DECLARE removed - counter never read */

struct backing_dev_info;

/* enum writeback_sync_modes and struct writeback_control removed - never used */

/* wb_domain struct removed - never used */
/* writeback_inodes_sb, try_to_writeback_inodes_sb, sync_inodes_sb removed - never called */
/* wakeup_flusher_threads, inode_wait_for_writeback, inode_io_list_del removed - never called */

/* wb_update_bandwidth, do_writepages removed - never called */

#endif		 
