
#include <linux/export.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include "internal.h"

static const struct constant_table bool_names[] = {
	{ "0",		false },
	{ "1",		true },
	{ "false",	false },
	{ "no",		false },
	{ "true",	true },
	{ "yes",	true },
	{ },
};

static const struct constant_table *
__lookup_constant(const struct constant_table *tbl, const char *name)
{
	for ( ; tbl->name; tbl++)
		if (strcmp(name, tbl->name) == 0)
			return tbl;
	return NULL;
}

int lookup_constant(const struct constant_table *tbl, const char *name, int not_found)
{
	const struct constant_table *p = __lookup_constant(tbl, name);

	return p ? p->value : not_found;
}

static inline bool is_flag(const struct fs_parameter_spec *p)
{
	return p->type == NULL;
}

static const struct fs_parameter_spec *fs_lookup_key(
	const struct fs_parameter_spec *desc,
	struct fs_parameter *param, bool *negated)
{
	const struct fs_parameter_spec *p, *other = NULL;
	const char *name = param->key;
	bool want_flag = param->type == fs_value_is_flag;

	*negated = false;
	for (p = desc; p->name; p++) {
		if (strcmp(p->name, name) != 0)
			continue;
		if (likely(is_flag(p) == want_flag))
			return p;
		other = p;
	}
	if (want_flag) {
		if (name[0] == 'n' && name[1] == 'o' && name[2]) {
			for (p = desc; p->name; p++) {
				if (strcmp(p->name, name + 2) != 0)
					continue;
				if (!(p->flags & fs_param_neg_with_no))
					continue;
				*negated = true;
				return p;
			}
		}
	}
	return other;
}

int __fs_parse(struct p_log *log,
	     const struct fs_parameter_spec *desc,
	     struct fs_parameter *param,
	     struct fs_parse_result *result)
{
	const struct fs_parameter_spec *p;

	result->uint_64 = 0;

	p = fs_lookup_key(desc, param, &result->negated);
	if (!p)
		return -ENOPARAM;

	if (p->flags & fs_param_deprecated)
		warn_plog(log, "Deprecated parameter '%s'", param->key);

	 
	if (is_flag(p)) {
		if (param->type != fs_value_is_flag)
			return inval_plog(log, "Unexpected value for '%s'",
				      param->key);
		result->boolean = !result->negated;
	} else  {
		int ret = p->type(log, p, param, result);
		if (ret)
			return ret;
	}
	return p->opt;
}

int fs_lookup_param(struct fs_context *fc,
		    struct fs_parameter *param,
		    bool want_bdev,
		    struct path *_path)
{
	struct filename *f;
	unsigned int flags = 0;
	bool put_f;
	int ret;

	switch (param->type) {
	case fs_value_is_string:
		f = getname_kernel(param->string);
		if (IS_ERR(f))
			return PTR_ERR(f);
		put_f = true;
		break;
	case fs_value_is_filename:
		f = param->name;
		put_f = false;
		break;
	default:
		return invalf(fc, "%s: not usable as path", param->key);
	}

	ret = filename_lookup(param->dirfd, f, flags, _path, NULL);
	if (ret < 0) {
		errorf(fc, "%s: Lookup failure for '%s'", param->key, f->name);
		goto out;
	}

	if (want_bdev &&
	    !S_ISBLK(d_backing_inode(_path->dentry)->i_mode)) {
		path_put(_path);
		_path->dentry = NULL;
		_path->mnt = NULL;
		errorf(fc, "%s: Non-blockdev passed as '%s'",
		       param->key, f->name);
		ret = -ENOTBLK;
	}

out:
	if (put_f)
		putname(f);
	return ret;
}

static int fs_param_bad_value(struct p_log *log, struct fs_parameter *param)
{
	return inval_plog(log, "Bad value for '%s'", param->key);
}

int fs_param_is_bool(struct p_log *log, const struct fs_parameter_spec *p,
		     struct fs_parameter *param, struct fs_parse_result *result)
{
	int b;
	if (param->type != fs_value_is_string)
		return fs_param_bad_value(log, param);
	if (!*param->string && (p->flags & fs_param_can_be_empty))
		return 0;
	b = lookup_constant(bool_names, param->string, -1);
	if (b == -1)
		return fs_param_bad_value(log, param);
	result->boolean = b;
	return 0;
}

int fs_param_is_u32(struct p_log *log, const struct fs_parameter_spec *p,
		    struct fs_parameter *param, struct fs_parse_result *result)
{
	int base = (unsigned long)p->data;
	if (param->type != fs_value_is_string)
		return fs_param_bad_value(log, param);
	if (!*param->string && (p->flags & fs_param_can_be_empty))
		return 0;
	if (kstrtouint(param->string, base, &result->uint_32) < 0)
		return fs_param_bad_value(log, param);
	return 0;
}

int fs_param_is_s32(struct p_log *log, const struct fs_parameter_spec *p,
		    struct fs_parameter *param, struct fs_parse_result *result)
{
	if (param->type != fs_value_is_string)
		return fs_param_bad_value(log, param);
	if (!*param->string && (p->flags & fs_param_can_be_empty))
		return 0;
	if (kstrtoint(param->string, 0, &result->int_32) < 0)
		return fs_param_bad_value(log, param);
	return 0;
}

int fs_param_is_u64(struct p_log *log, const struct fs_parameter_spec *p,
		    struct fs_parameter *param, struct fs_parse_result *result)
{
	if (param->type != fs_value_is_string)
		return fs_param_bad_value(log, param);
	if (!*param->string && (p->flags & fs_param_can_be_empty))
		return 0;
	if (kstrtoull(param->string, 0, &result->uint_64) < 0)
		return fs_param_bad_value(log, param);
	return 0;
}

int fs_param_is_enum(struct p_log *log, const struct fs_parameter_spec *p,
		     struct fs_parameter *param, struct fs_parse_result *result)
{
	const struct constant_table *c;
	if (param->type != fs_value_is_string)
		return fs_param_bad_value(log, param);
	if (!*param->string && (p->flags & fs_param_can_be_empty))
		return 0;
	c = __lookup_constant(p->data, param->string);
	if (!c)
		return fs_param_bad_value(log, param);
	result->uint_32 = c->value;
	return 0;
}

int fs_param_is_string(struct p_log *log, const struct fs_parameter_spec *p,
		       struct fs_parameter *param, struct fs_parse_result *result)
{
	if (param->type != fs_value_is_string ||
	    (!*param->string && !(p->flags & fs_param_can_be_empty)))
		return fs_param_bad_value(log, param);
	return 0;
}

int fs_param_is_blob(struct p_log *log, const struct fs_parameter_spec *p,
		     struct fs_parameter *param, struct fs_parse_result *result)
{
	if (param->type != fs_value_is_blob)
		return fs_param_bad_value(log, param);
	return 0;
}

int fs_param_is_fd(struct p_log *log, const struct fs_parameter_spec *p,
		  struct fs_parameter *param, struct fs_parse_result *result)
{
	switch (param->type) {
	case fs_value_is_string:
		if ((!*param->string && !(p->flags & fs_param_can_be_empty)) ||
		    kstrtouint(param->string, 0, &result->uint_32) < 0)
			break;
		if (result->uint_32 <= INT_MAX)
			return 0;
		break;
	case fs_value_is_file:
		result->uint_32 = param->dirfd;
		if (result->uint_32 <= INT_MAX)
			return 0;
		break;
	default:
		break;
	}
	return fs_param_bad_value(log, param);
}

int fs_param_is_blockdev(struct p_log *log, const struct fs_parameter_spec *p,
		  struct fs_parameter *param, struct fs_parse_result *result)
{
	return 0;
}

int fs_param_is_path(struct p_log *log, const struct fs_parameter_spec *p,
		     struct fs_parameter *param, struct fs_parse_result *result)
{
	return 0;
}

