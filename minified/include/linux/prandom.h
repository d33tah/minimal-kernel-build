#ifndef _LINUX_PRANDOM_H
#define _LINUX_PRANDOM_H

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/random.h>

static inline u32 prandom_u32(void)
{
	return get_random_u32();
}


#endif
