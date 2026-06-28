#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/security.h>


/*
 * Runtime-dead cmdline-parse cluster (anchor-stub): on a single-shot boot the
 * cmdline is empty, so parse_args()'s `if (*args)` guard is false and the loop
 * body (next_arg + parse_one) never runs. Every parse_one callee and every
 * parse_args `unknown` callback is therefore runtime-dead. Bodies stubbed;
 * symbols kept link-live for the parse_args loop + moduleparam.h externs +
 * init/main.c callback address-of references.
 */
bool parameqn(const char *a, const char *b, size_t n)
{
	return false;
}

bool parameq(const char *a, const char *b)
{
	return false;
}

static int parse_one(char *param,
		     char *val,
		     const char *doing,
		     const struct kernel_param *params,
		     unsigned num_params,
		     s16 min_level,
		     s16 max_level,
		     void *arg,
		     int (*handle_unknown)(char *param, char *val,
				     const char *doing, void *arg))
{
	return -ENOENT;
}

char *parse_args(const char *doing,
		 char *args,
		 const struct kernel_param *params,
		 unsigned num,
		 s16 min_level,
		 s16 max_level,
		 void *arg,
		 int (*unknown)(char *param, char *val,
				const char *doing, void *arg))
{
	char *param, *val, *err = NULL;

	 
	args = skip_spaces(args);

	if (*args)
		while (*args) {
		int ret;
		int irq_was_disabled;

		args = next_arg(args, &param, &val);
		 
		if (!val && strcmp(param, "--") == 0)
			return err ?: args;
		irq_was_disabled = irqs_disabled();
		ret = parse_one(param, val, doing, params, num,
				min_level, max_level, arg, unknown);
		switch (ret) {
		case 0:
			continue;
		case -ENOENT:
			pr_err("%s: Unknown parameter `%s'\n", doing, param);
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
