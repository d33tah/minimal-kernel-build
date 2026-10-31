#ifndef _LINUX_JIFFIES_H
#define _LINUX_JIFFIES_H

#include <asm/param.h>
#include <linux/cache.h>
#include <linux/limits.h>

#define MSEC_PER_SEC 1000L

#define __jiffy_arch_data

extern u64 __cacheline_aligned_in_smp jiffies_64;
extern unsigned long volatile __cacheline_aligned_in_smp __jiffy_arch_data
	jiffies;

#define INITIAL_JIFFIES ((unsigned long)(unsigned int)(-300 * HZ))

#endif
