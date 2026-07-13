#ifndef _LINUX_JIFFIES_H
#define _LINUX_JIFFIES_H

#include <linux/cache.h>
#include <linux/limits.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/time.h>
#include <asm/param.h>

/* Inlined from vdso/jiffies.h */
#define TICK_NSEC ((NSEC_PER_SEC+HZ/2)/HZ)

#include <generated/timeconst.h>

extern int register_refined_jiffies(long clock_tick_rate);

#ifndef __jiffy_arch_data
#define __jiffy_arch_data
#endif

extern u64 __cacheline_aligned_in_smp jiffies_64;
extern unsigned long volatile __cacheline_aligned_in_smp __jiffy_arch_data jiffies;

#define time_after(a,b)			(typecheck(unsigned long, a) && 	 typecheck(unsigned long, b) && 	 ((long)((b) - (a)) < 0))
#define time_before(a,b)	time_after(b,a)




#define INITIAL_JIFFIES ((unsigned long)(unsigned int) (-300*HZ))

#define MAX_JIFFY_OFFSET ((LONG_MAX >> 1)-1)

/*
 * HZ is fixed to CONFIG_HZ==250 in this build (Kconfig "default HZ_250";
 * include/generated/autoconf.h "#define CONFIG_HZ 250"; HZ==CONFIG_HZ via
 * include/asm-generic/param.h).  MSEC_PER_SEC==1000L.  So the controlling
 * expression "HZ <= MSEC_PER_SEC && !(MSEC_PER_SEC % HZ)" folds to
 * "250 <= 1000 && !(1000 % 250)" == TRUE.  The former "#elif HZ > MSEC_PER_SEC"
 * and "#else" arms are therefore statically dead in every TU; both additionally
 * referenced symbols that no longer exist in this tree (jiffies_to_msecs was
 * removed as never-called, and MSEC_TO_HZ_MUL32/ADJ32/SHR32 are undefined),
 * so they could never have compiled if enabled.  Emit the live arm only.
 */
static inline unsigned long _msecs_to_jiffies(const unsigned int m) {
	return (m + (MSEC_PER_SEC / HZ) - 1) / (MSEC_PER_SEC / HZ); }
static __always_inline unsigned long msecs_to_jiffies(const unsigned int m) {
	if ((int)m < 0)
		return MAX_JIFFY_OFFSET;
	return _msecs_to_jiffies(m); }

#endif
