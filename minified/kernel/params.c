#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/ctype.h>

static inline char *skip_spaces(const char *str)
{
	while (isspace(*str))
		++str;
	return (char *)str;
}

/* Merged from lib/cmdline.c - only used here */
static char *next_arg(char *args, char **param, char **val)
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

bool parameqn(const char *a, const char *b, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		char ca = (a[i] == '-') ? '_' : a[i];
		char cb = (b[i] == '-') ? '_' : b[i];
		if (ca != cb)
			return false;
	}
	return true;
}

bool parameq(const char *a, const char *b)
{
	return parameqn(a, b, strlen(a) + 1);
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
			int ret = -ENOENT;
			unsigned int i;

			args = next_arg(args, &param, &val);

			if (!val && strcmp(param, "--") == 0)
				return err ?: args;

			for (i = 0; i < num; i++) {
				if (parameq(param, params[i].name)) {
					if (params[i].level < min_level ||
					    params[i].level > max_level) {
						ret = 0;
						break;
					}
					if (!val &&
					    !(params[i].ops->flags &
					      KERNEL_PARAM_OPS_FL_NOARG)) {
						ret = -EINVAL;
						break;
					}
					if (params[i].flags &
					    KERNEL_PARAM_FL_UNSAFE)
						add_taint(TAINT_USER,
							  LOCKDEP_STILL_OK);
					ret = params[i].ops->set(val,
								 &params[i]);
					break;
				}
			}
			if (ret == -ENOENT && unknown)
				ret = unknown(param, val, doing, arg);

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
