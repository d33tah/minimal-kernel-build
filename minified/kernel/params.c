#include <linux/string.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/ctype.h>

static inline char *skip_spaces(const char *str)
{
	while (isspace(*str))
		++str;
	return (char *)str;
}

/* Simplified: no quote handling needed for hello-world kernel */
static char *next_arg(char *args, char **param, char **val)
{
	unsigned int i, equals = 0;

	for (i = 0; args[i]; i++) {
		if (isspace(args[i]))
			break;
		if (equals == 0 && args[i] == '=')
			equals = i;
	}

	*param = args;
	if (!equals)
		*val = NULL;
	else {
		args[equals] = '\0';
		*val = args + equals + 1;
	}

	if (args[i]) {
		args[i] = '\0';
		args += i + 1;
	} else
		args += i;

	return skip_spaces(args);
}

bool parameq(const char *a, const char *b)
{
	size_t n = strlen(a) + 1;
	size_t i;

	for (i = 0; i < n; i++) {
		char ca = (a[i] == '-') ? '_' : a[i];
		char cb = (b[i] == '-') ? '_' : b[i];
		if (ca != cb)
			return false;
	}
	return true;
}

char *
parse_args(const char *doing, char *args, const struct kernel_param *params,
	   unsigned num, s16 min_level, s16 max_level, void *arg,
	   int (*unknown)(char *param, char *val, const char *doing, void *arg))
{
	char *param, *val, *err = NULL;

	args = skip_spaces(args);

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
				if (!val && !(params[i].ops->flags &
					      KERNEL_PARAM_OPS_FL_NOARG)) {
					ret = -EINVAL;
					break;
				}
				ret = params[i].ops->set(val, &params[i]);
				break;
			}
		}
		if (ret == -ENOENT && unknown)
			ret = unknown(param, val, doing, arg);

		if (ret)
			err = ERR_PTR(ret);
	}

	return err;
}
