#ifndef _LINUX_BACKING_DEV_H
#define _LINUX_BACKING_DEV_H
/* linux/kernel.h removed - no kernel.h macros used */
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/writeback.h>
#include <linux/backing-dev-defs.h>
#include <linux/slab.h>
void bdi_put(struct backing_dev_info *bdi);
void bdi_unregister(struct backing_dev_info *bdi);
void wb_workfn(struct work_struct *work);
/* bdi_set_min_ratio, bdi_set_max_ratio removed - never called */
#define BDI_CAP_WRITEBACK		(1 << 0)
/* BDI_CAP_WRITEBACK_ACCT removed - unused */
extern struct backing_dev_info noop_backing_dev_info;
int bdi_init(struct backing_dev_info *bdi);
struct backing_dev_info *inode_to_bdi(struct inode *inode);
static inline bool mapping_can_writeback(struct address_space *mapping) { return inode_to_bdi(mapping->host)->capabilities & BDI_CAP_WRITEBACK; }
/* inode_to_wb removed - unused */
#endif
