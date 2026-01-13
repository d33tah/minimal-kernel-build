#ifndef _LINUX_RANDOM_H
#define _LINUX_RANDOM_H

#include <linux/types.h>
#include <linux/init.h>

void get_random_bytes(void *buf, size_t len);
u32 get_random_u32(void);
static inline unsigned int get_random_int(void) { return get_random_u32(); }
/* random_init removed - empty stub */

#ifndef MODULE
extern const struct file_operations random_fops, urandom_fops;
#endif

#endif
