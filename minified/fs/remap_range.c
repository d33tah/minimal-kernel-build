// SPDX-License-Identifier: GPL-2.0-only
/* Stubbed remap_range.c */
#include <linux/fs.h>
#include <linux/export.h>

int generic_remap_file_range_prep(struct file *file_in, loff_t pos_in,
				  struct file *file_out, loff_t pos_out,
				  loff_t *count, unsigned int remap_flags) { return -EOPNOTSUPP; }

loff_t do_clone_file_range(struct file *file_in, loff_t pos_in,
			   struct file *file_out, loff_t pos_out,
			   loff_t len, unsigned int remap_flags) { return -EOPNOTSUPP; }

loff_t vfs_clone_file_range(struct file *file_in, loff_t pos_in,
			    struct file *file_out, loff_t pos_out,
			    loff_t len, unsigned int remap_flags) { return -EOPNOTSUPP; }

loff_t vfs_dedupe_file_range_one(struct file *src_file, loff_t src_pos,
				 struct file *dst_file, loff_t dst_pos,
				 loff_t len, unsigned int remap_flags) { return -EOPNOTSUPP; }

int vfs_dedupe_file_range(struct file *file, struct file_dedupe_range *same) { return -EOPNOTSUPP; }
