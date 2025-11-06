// SPDX-License-Identifier: GPL-2.0-or-later
/* Stubbed fs_context.c */
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/fs.h>
#include <linux/export.h>

int vfs_parse_fs_param_source(struct fs_context *fc, struct fs_parameter *param) { return 0; }
EXPORT_SYMBOL(vfs_parse_fs_param_source);

int vfs_parse_fs_param(struct fs_context *fc, struct fs_parameter *param) { return 0; }
EXPORT_SYMBOL(vfs_parse_fs_param);

int vfs_parse_fs_string(struct fs_context *fc, const char *key,
			const char *value, size_t v_size) { return 0; }
EXPORT_SYMBOL(vfs_parse_fs_string);

int generic_parse_monolithic(struct fs_context *fc, void *data) { return 0; }
EXPORT_SYMBOL(generic_parse_monolithic);

struct fs_context *fs_context_for_mount(struct file_system_type *fs_type,
					unsigned int sb_flags) { return ERR_PTR(-ENOMEM); }
EXPORT_SYMBOL(fs_context_for_mount);

struct fs_context *fs_context_for_reconfigure(struct dentry *dentry,
					      unsigned int sb_flags,
					      unsigned int sb_flags_mask) { return ERR_PTR(-ENOMEM); }
EXPORT_SYMBOL(fs_context_for_reconfigure);

struct fs_context *fs_context_for_submount(struct file_system_type *type,
					   struct dentry *reference) { return ERR_PTR(-ENOMEM); }
EXPORT_SYMBOL(fs_context_for_submount);

struct fs_context *vfs_dup_fs_context(struct fs_context *src_fc) { return ERR_PTR(-ENOMEM); }
EXPORT_SYMBOL(vfs_dup_fs_context);

void logfc(struct fc_log *log, const char *prefix, char level, const char *fmt, ...) { }
EXPORT_SYMBOL(logfc);

void put_fs_context(struct fs_context *fc) { }
EXPORT_SYMBOL(put_fs_context);
