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

struct kmalloced_param {
	struct list_head list;
	char val[];
};
static LIST_HEAD(kmalloced_params);
static DEFINE_SPINLOCK(kmalloced_params_lock);

static void *kmalloc_parameter(unsigned int size)
{
	struct kmalloced_param *p;

	p = kmalloc(sizeof(*p) + size, GFP_KERNEL);
	if (!p)
		return NULL;

	spin_lock(&kmalloced_params_lock);
	list_add(&p->list, &kmalloced_params);
	spin_unlock(&kmalloced_params_lock);

	return p->val;
}

static void maybe_kfree_parameter(void *param)
{
	struct kmalloced_param *p;

	spin_lock(&kmalloced_params_lock);
	list_for_each_entry(p, &kmalloced_params, list) {
		if (p->val == param) {
			list_del(&p->list);
			kfree(p);
			break;
		}
	}
	spin_unlock(&kmalloced_params_lock);
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

static bool param_check_unsafe(const struct kernel_param *kp)
{
	if (kp->flags & KERNEL_PARAM_FL_HWPARAM &&
	    security_locked_down(LOCKDOWN_MODULE_PARAMETERS))
		return false;

	if (kp->flags & KERNEL_PARAM_FL_UNSAFE) {
		add_taint(TAINT_USER, LOCKDEP_STILL_OK);
	}

	return true;
}

static int parse_one(char *param, char *val, const char *doing,
		     const struct kernel_param *params, unsigned num_params,
		     s16 min_level, s16 max_level, void *arg,
		     int (*handle_unknown)(char *param, char *val,
					   const char *doing, void *arg))
{
	unsigned int i;
	int err;

	for (i = 0; i < num_params; i++) {
		if (parameq(param, params[i].name)) {
			if (params[i].level < min_level ||
			    params[i].level > max_level)
				return 0;

			if (!val &&
			    !(params[i].ops->flags & KERNEL_PARAM_OPS_FL_NOARG))
				return -EINVAL;
			kernel_param_lock(params[i].mod);
			if (param_check_unsafe(&params[i]))
				err = params[i].ops->set(val, &params[i]);
			else
				err = -EPERM;
			kernel_param_unlock(params[i].mod);
			return err;
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

/* Stub: module parameters not needed for Hello World kernel */
#define STANDARD_PARAM_DEF(name, type, format, strtolfn)                     \
	int param_set_##name(const char *val, const struct kernel_param *kp) \
	{                                                                    \
		return 0;                                                    \
	}                                                                    \
	int param_get_##name(char *buffer, const struct kernel_param *kp)    \
	{                                                                    \
		return 0;                                                    \
	}                                                                    \
	const struct kernel_param_ops param_ops_##name = {                   \
		.set = param_set_##name,                                     \
		.get = param_get_##name,                                     \
	}

STANDARD_PARAM_DEF(byte, unsigned char, "%hhu", kstrtou8);
STANDARD_PARAM_DEF(short, short, "%hi", kstrtos16);
STANDARD_PARAM_DEF(ushort, unsigned short, "%hu", kstrtou16);
STANDARD_PARAM_DEF(int, int, "%i", kstrtoint);
STANDARD_PARAM_DEF(uint, unsigned int, "%u", kstrtouint);
STANDARD_PARAM_DEF(long, long, "%li", kstrtol);
STANDARD_PARAM_DEF(ulong, unsigned long, "%lu", kstrtoul);
STANDARD_PARAM_DEF(ullong, unsigned long long, "%llu", kstrtoull);
STANDARD_PARAM_DEF(hexint, unsigned int, "%#08x", kstrtouint);

int param_set_charp(const char *val, const struct kernel_param *kp)
{
	return 0;
}
int param_get_charp(char *buffer, const struct kernel_param *kp)
{
	return 0;
}
const struct kernel_param_ops param_ops_charp = { .set = param_set_charp,
						  .get = param_get_charp };

int param_set_bool(const char *val, const struct kernel_param *kp)
{
	return 0;
}
int param_get_bool(char *buffer, const struct kernel_param *kp)
{
	return 0;
}
const struct kernel_param_ops param_ops_bool = { .set = param_set_bool,
						 .get = param_get_bool };

#define to_module_attr(n) container_of(n, struct module_attribute, attr)
#define to_module_kobject(n) container_of(n, struct module_kobject, kobj)

struct param_attribute {
	struct module_attribute mattr;
	const struct kernel_param *param;
};

struct module_param_attrs {
	unsigned int num;
	struct attribute_group grp;
	struct param_attribute attrs[];
};

#define __modinit __init
