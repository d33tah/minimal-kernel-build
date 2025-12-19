
#ifndef _LINUX_BACKING_DEV_H
#define _LINUX_BACKING_DEV_H

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/writeback.h>
#include <linux/backing-dev-defs.h>
#include <linux/slab.h>

/* bdi_get removed - never called */

void bdi_put(struct backing_dev_info *bdi);
void bdi_unregister(struct backing_dev_info *bdi);

struct backing_dev_info *bdi_alloc(int node_id);

void wb_start_background_writeback(struct bdi_writeback *wb);
void wb_workfn(struct work_struct *work);

/* wb_wait_for_completion removed - never called */
/* wb_has_dirty_io, bdi_has_dirty_io, wb_stat_mod, inc_wb_stat, dec_wb_stat,
   wb_stat, wb_stat_sum, wb_stat_error removed - never called */

int bdi_set_min_ratio(struct backing_dev_info *bdi, unsigned int min_ratio);
int bdi_set_max_ratio(struct backing_dev_info *bdi, unsigned int max_ratio);

#define BDI_CAP_WRITEBACK		(1 << 0)
#define BDI_CAP_WRITEBACK_ACCT		(1 << 1)

extern struct backing_dev_info noop_backing_dev_info;

int bdi_init(struct backing_dev_info *bdi);

/* writeback_in_progress removed - never called */

struct backing_dev_info *inode_to_bdi(struct inode *inode);

static inline bool mapping_can_writeback(struct address_space *mapping)
{
	return inode_to_bdi(mapping->host)->capabilities & BDI_CAP_WRITEBACK;
}

/* bdi_sched_wait, inode_cgwb_enabled, wb_find_current, wb_get_create_current,
   inode_to_wb_is_valid removed - never called */

static inline struct bdi_writeback *inode_to_wb(struct inode *inode)
{
	return &inode_to_bdi(inode)->wb;
}

/* inode_to_wb_wbc, unlocked_inode_to_wb_begin, unlocked_inode_to_wb_end
   removed - never called */


/* bdi_dev_name removed - unused */

#endif
