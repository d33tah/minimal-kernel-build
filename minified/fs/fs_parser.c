// SPDX-License-Identifier: GPL-2.0-or-later
/* Stubbed fs_parser.c */
#include <linux/fs_parser.h>
#include <linux/export.h>

int lookup_constant(const struct constant_table *tbl, const char *name, int not_found) { return not_found; }
EXPORT_SYMBOL(lookup_constant);

int __fs_parse(struct p_log *log, const struct fs_parameter_spec *desc,
	       struct fs_parameter *param, struct fs_parse_result *result) { return -ENOPARAM; }
EXPORT_SYMBOL(__fs_parse);

int fs_lookup_param(struct fs_context *fc, struct fs_parameter *param,
		    bool want_bdev, struct path *_path) { return -ENOENT; }
EXPORT_SYMBOL(fs_lookup_param);

int fs_param_is_bool(struct p_log *log, const struct fs_parameter_spec *p,
		     struct fs_parameter *param, struct fs_parse_result *result) { return 0; }
int fs_param_is_u32(struct p_log *log, const struct fs_parameter_spec *p,
		    struct fs_parameter *param, struct fs_parse_result *result) { return 0; }
int fs_param_is_s32(struct p_log *log, const struct fs_parameter_spec *p,
		    struct fs_parameter *param, struct fs_parse_result *result) { return 0; }
int fs_param_is_u64(struct p_log *log, const struct fs_parameter_spec *p,
		    struct fs_parameter *param, struct fs_parse_result *result) { return 0; }
int fs_param_is_enum(struct p_log *log, const struct fs_parameter_spec *p,
		     struct fs_parameter *param, struct fs_parse_result *result) { return 0; }
int fs_param_is_string(struct p_log *log, const struct fs_parameter_spec *p,
			struct fs_parameter *param, struct fs_parse_result *result) { return 0; }
int fs_param_is_blob(struct p_log *log, const struct fs_parameter_spec *p,
		     struct fs_parameter *param, struct fs_parse_result *result) { return 0; }
int fs_param_is_blockdev(struct p_log *log, const struct fs_parameter_spec *p,
			 struct fs_parameter *param, struct fs_parse_result *result) { return 0; }
int fs_param_is_path(struct p_log *log, const struct fs_parameter_spec *p,
		     struct fs_parameter *param, struct fs_parse_result *result) { return 0; }
int fs_param_is_fd(struct p_log *log, const struct fs_parameter_spec *p,
		   struct fs_parameter *param, struct fs_parse_result *result) { return 0; }
