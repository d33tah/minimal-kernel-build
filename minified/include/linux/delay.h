#ifndef _LINUX_DELAY_H
#define _LINUX_DELAY_H


#include <linux/math.h>
#include <linux/sched.h>

extern unsigned long loops_per_jiffy;

#include <asm/delay.h>


#ifndef MAX_UDELAY_MS
#define MAX_UDELAY_MS	5
#endif

#ifndef mdelay
#define mdelay(n) (\
	(__builtin_constant_p(n) && (n)<=MAX_UDELAY_MS) ? udelay((n)*1000) : \
	({unsigned long __ms=(n); while (__ms--) udelay(1000);}))
#endif

#ifndef ndelay
static inline void ndelay(unsigned long x)
{
	udelay(DIV_ROUND_UP(x, 1000));
}
#define ndelay(x) ndelay(x)
#endif

/* lpj_fine removed - only set, never read */
void calibrate_delay(void);
/* calibration_delay_done removed - only called from calibrate.c */
void msleep(unsigned int msecs);
unsigned long msleep_interruptible(unsigned int msecs);
static inline void ssleep(unsigned int seconds)
{
	msleep(seconds * 1000);
}

#endif  
