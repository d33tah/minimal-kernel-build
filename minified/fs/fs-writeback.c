// SPDX-License-Identifier: GPL-2.0-only
/* Minimal stub for fs-writeback.c - writeback not needed for read-only minimal kernel */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>

void inode_io_list_del(struct inode *inode) { }

void __mark_inode_dirty(struct inode *inode, int flags) { }

void writeback_inodes_sb_nr(struct super_block *sb, unsigned long nr,
			    enum wb_reason reason) { }

void writeback_inodes_sb(struct super_block *sb, enum wb_reason reason) { }

void try_to_writeback_inodes_sb(struct super_block *sb, enum wb_reason reason) { }

void sync_inodes_sb(struct super_block *sb) { }

int write_inode_now(struct inode *inode, int sync) { return 0; }

int sync_inode_metadata(struct inode *inode, int wait) { return 0; }

void wb_workfn(struct work_struct *work) { }

int dirty_writeback_centisecs = 500;
int dirty_expire_centisecs = 3000;

void wb_start_background_writeback(struct bdi_writeback *wb) { }
void wakeup_flusher_threads_bdi(struct backing_dev_info *bdi, enum wb_reason reason) { }
void sb_clear_inode_writeback(struct inode *inode) { }
void sb_mark_inode_writeback(struct inode *inode) { }
void wakeup_flusher_threads(enum wb_reason reason) { }
void inode_wait_for_writeback(struct inode *inode) { }
