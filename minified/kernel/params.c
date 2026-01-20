#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/security.h>

/* Merged from lib/cmdline.c - only used here */
char *next_arg(char *args, char **param, char **val)
{
	unsigned int i, equals = 0;
	int in_quote = 0, quoted = 0;

	if (*args == '"') {
		args++;
		in_quote = 1;
		quoted = 1;
	}

	for (i = 0; args[i]; i++) {
		if (isspace(args[i]) && !in_quote)
			break;
		if (equals == 0) {
			if (args[i] == '=')
				equals = i;
		}
		if (args[i] == '"')
			in_quote = !in_quote;
	}

	*param = args;
	if (!equals)
		*val = NULL;
	else {
		args[equals] = '\0';
		*val = args + equals + 1;

		if (**val == '"') {
			(*val)++;
			if (args[i - 1] == '"')
				args[i - 1] = '\0';
		}
	}
	if (quoted && args[i - 1] == '"')
		args[i - 1] = '\0';

	if (args[i]) {
		args[i] = '\0';
		args += i + 1;
	} else
		args += i;

	return skip_spaces(args);
}

static char dash2underscore(char c)
{
	if (c == '-')
		return '_';
	return c;
}

bool parameqn(const char *a, const char *b, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (dash2underscore(a[i]) != dash2underscore(b[i]))
			return false;
	}
	return true;
}

bool parameq(const char *a, const char *b)
{
	return parameqn(a, b, strlen(a) + 1);
}

/* param_check_unsafe inlined - always returns true */

static int parse_one(char *param, char *val, const char *doing,
		     const struct kernel_param *params, unsigned num_params,
		     s16 min_level, s16 max_level, void *arg,
		     int (*handle_unknown)(char *param, char *val,
					   const char *doing, void *arg))
{
	unsigned int i;

	for (i = 0; i < num_params; i++) {
		if (parameq(param, params[i].name)) {
			if (params[i].level < min_level ||
			    params[i].level > max_level)
				return 0;

			if (!val &&
			    !(params[i].ops->flags & KERNEL_PARAM_OPS_FL_NOARG))
				return -EINVAL;
			if (params[i].flags & KERNEL_PARAM_FL_UNSAFE)
				add_taint(TAINT_USER, LOCKDEP_STILL_OK);
			return params[i].ops->set(val, &params[i]);
		}
	}

	if (handle_unknown) {
		return handle_unknown(param, val, doing, arg);
	}

	return -ENOENT;
}

char *
parse_args(const char *doing, char *args, const struct kernel_param *params,
	   unsigned num, s16 min_level, s16 max_level, void *arg,
	   int (*unknown)(char *param, char *val, const char *doing, void *arg))
{
	char *param, *val, *err = NULL;

	args = skip_spaces(args);

	if (*args)
		while (*args) {
			int ret;

			args = next_arg(args, &param, &val);

			if (!val && strcmp(param, "--") == 0)
				return err ?: args;
			ret = parse_one(param, val, doing, params, num,
					min_level, max_level, arg, unknown);
			switch (ret) {
			case 0:
				continue;
			case -ENOENT:
				pr_err("%s: Unknown parameter `%s'\n", doing,
				       param);
				break;
			case -ENOSPC:
				pr_err("%s: `%s' too large for parameter `%s'\n",
				       doing, val ?: "", param);
				break;
			default:
				pr_err("%s: `%s' invalid for parameter `%s'\n",
				       doing, val ?: "", param);
				break;
			}

			err = ERR_PTR(ret);
		}

	return err;
}

/* param_ops_*, to_module_attr, to_module_kobject, param_attribute,
   module_param_attrs, __modinit removed - module parameters never used */
