#ifndef _LINUX_RANDOM_H
#define _LINUX_RANDOM_H

#include <linux/types.h>
#include <linux/init.h>

void get_random_bytes(void *buf, size_t len);
/* get_random_u32, get_random_int, random_init removed - no callers */

#ifndef MODULE
extern const struct file_operations random_fops, urandom_fops;
#endif

#endif
