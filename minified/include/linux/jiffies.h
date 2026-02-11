#ifndef _LINUX_JIFFIES_H
#define _LINUX_JIFFIES_H

#include <linux/cache.h>
#include <linux/limits.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/time64.h>
#include <asm/param.h>

/* Inlined from vdso/jiffies.h */
#define TICK_NSEC ((NSEC_PER_SEC+HZ/2)/HZ)

#include <generated/timeconst.h>

/* SHIFT_HZ conditionals removed - never used */

/* register_refined_jiffies declaration removed - function removed */

#ifndef __jiffy_arch_data
#define __jiffy_arch_data
#endif

extern u64 __cacheline_aligned_in_smp jiffies_64;
extern unsigned long volatile __cacheline_aligned_in_smp __jiffy_arch_data jiffies;

/* BITS_PER_LONG == 32 */
/* get_jiffies_64 declaration removed - function removed */

/* time_after removed - no callers */

/* time_after_eq, time_before_eq removed - never used */

#define INITIAL_JIFFIES ((unsigned long)(unsigned int) (-300*HZ))

/* MAX_JIFFY_OFFSET removed - only used in __msecs_to_jiffies which was removed */
/* preset_lpj removed - never assigned, always zero */


/* SEC_JIFFIE_SC, NSEC_JIFFIE_SC, SEC_CONVERSION, NSEC_CONVERSION removed - unused */

/* jiffies_to_msecs removed - never called */
/* jiffies_to_usecs removed - never called */


/* __msecs_to_jiffies, _msecs_to_jiffies, msecs_to_jiffies, __usecs_to_jiffies, usecs_to_jiffies removed - never called */
/* timespec64_to_jiffies, nsecs_to_jiffies64, nsecs_to_jiffies removed - never called */

#endif
