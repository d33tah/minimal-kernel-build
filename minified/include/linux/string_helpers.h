#ifndef _LINUX_STRING_HELPERS_H_
#define _LINUX_STRING_HELPERS_H_

#include <linux/bits.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/types.h>

struct device;
struct file;
struct task_struct;

enum string_size_units {
	STRING_UNITS_10,
};

void string_get_size(u64 size, u64 blk_size, enum string_size_units units,
		     char *buf, int len);

static inline void string_upper(char *dst, const char *src)
{
	do {
		*dst++ = toupper(*src);
	} while (*src++);
}

static inline void string_lower(char *dst, const char *src)
{
	do {
		*dst++ = tolower(*src);
	} while (*src++);
}

char *kstrdup_quotable(const char *src, gfp_t gfp);
char *kstrdup_quotable_cmdline(struct task_struct *task, gfp_t gfp);
char *kstrdup_quotable_file(struct file *file, gfp_t gfp);

char **kasprintf_strarray(gfp_t gfp, const char *prefix, size_t n);
void kfree_strarray(char **array, size_t n);

char **devm_kasprintf_strarray(struct device *dev, const char *prefix, size_t n);

static inline const char *str_yes_no(bool v)
{
	return v ? "yes" : "no";
}

static inline const char *str_on_off(bool v)
{
	return v ? "on" : "off";
}

static inline const char *str_enable_disable(bool v)
{
	return v ? "enable" : "disable";
}

static inline const char *str_enabled_disabled(bool v)
{
	return v ? "enabled" : "disabled";
}

#endif
