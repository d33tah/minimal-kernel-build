
#include <linux/fs_parser.h>

static inline bool is_flag(const struct fs_parameter_spec *p)
{
	return p->type == NULL;
}

int __fs_parse(struct p_log *log, const struct fs_parameter_spec *desc,
	       struct fs_parameter *param, struct fs_parse_result *result)
{
	const struct fs_parameter_spec *p, *other = NULL;
	const char *name = param->key;
	bool want_flag = param->type == fs_value_is_flag;

	result->uint_64 = 0;
	result->negated = false;

	for (p = desc; p->name; p++) {
		if (strcmp(p->name, name) != 0)
			continue;
		if (likely(is_flag(p) == want_flag))
			goto found;
		other = p;
	}
	p = other;
	if (!p)
		return -ENOPARAM;
found:
	if (is_flag(p)) {
		if (param->type != fs_value_is_flag)
			return inval_plog(log, "Unexpected value for '%s'",
					  param->key);
		result->boolean = !result->negated;
	} else {
		int ret = p->type(log, p, param, result);
		if (ret)
			return ret;
	}
	return p->opt;
}

int fs_param_is_u32(struct p_log *log, const struct fs_parameter_spec *p,
		    struct fs_parameter *param, struct fs_parse_result *result)
{
	int base = (unsigned long)p->data;
	if (param->type != fs_value_is_string)
		return inval_plog(log, "Bad value for '%s'", param->key);
	if (kstrtouint(param->string, base, &result->uint_32) < 0)
		return inval_plog(log, "Bad value for '%s'", param->key);
	return 0;
}
