#ifndef _LINUX_JIFFIES_H
#define _LINUX_JIFFIES_H

#include <linux/minmax.h>
#include <linux/timex.h>
#include <asm/param.h>

#define TICK_NSEC ((NSEC_PER_SEC+HZ/2)/HZ)

#ifndef __jiffy_arch_data
#define __jiffy_arch_data
#endif

extern u64 __cacheline_aligned_in_smp jiffies_64;
extern unsigned long volatile __cacheline_aligned_in_smp __jiffy_arch_data jiffies;

/* BITS_PER_LONG == 32 */

#define INITIAL_JIFFIES ((unsigned long)(unsigned int) (-300*HZ))

#endif
