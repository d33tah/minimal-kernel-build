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


#endif
