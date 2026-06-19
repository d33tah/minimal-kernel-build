
#ifndef _LINUX_RANDOM_H
#define _LINUX_RANDOM_H

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/irqnr.h>

struct notifier_block;

void get_random_bytes(void *buf, size_t len);
u32 get_random_u32(void);
static inline unsigned int get_random_int(void)
{
	return get_random_u32();
}
static inline unsigned long get_random_long(void)
{
	return get_random_u32(); /* BITS_PER_LONG == 32 */
}

# define CANARY_MASK 0xffffffffUL

bool rng_is_initialized(void);

#endif
