#ifndef _LINUX_KASAN_CHECKS_H
#define _LINUX_KASAN_CHECKS_H

#include <linux/types.h>
#include <linux/stddef.h>

static inline bool kasan_check_read(const volatile void *p, unsigned int size)
{
	return true;
}
static inline bool kasan_check_write(const volatile void *p, unsigned int size)
{
	return true;
}

#endif
