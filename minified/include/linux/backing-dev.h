
#ifndef _LINUX_BACKING_DEV_H
#define _LINUX_BACKING_DEV_H

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/writeback.h>
#include <linux/backing-dev-defs.h>
#include <linux/slab.h>


/* wb_has_dirty_io, bdi_has_dirty_io, wb_stat_mod, inc_wb_stat, dec_wb_stat,
   wb_stat, wb_stat_sum, wb_stat_error removed - never called */

/* inode_to_bdi, inode_to_wb (and bdi_sched_wait, inode_cgwb_enabled,
   wb_find_current, wb_get_create_current, inode_to_wb_is_valid,
   inode_to_wb_wbc, unlocked_inode_to_wb_begin/end) removed - never called */



#endif
