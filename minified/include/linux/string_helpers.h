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


char *kstrdup_quotable(const char *src, gfp_t gfp);
char *kstrdup_quotable_cmdline(struct task_struct *task, gfp_t gfp);
char *kstrdup_quotable_file(struct file *file, gfp_t gfp);

char **kasprintf_strarray(gfp_t gfp, const char *prefix, size_t n);
void kfree_strarray(char **array, size_t n);

char **devm_kasprintf_strarray(struct device *dev, const char *prefix, size_t n);


#endif
