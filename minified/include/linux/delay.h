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
/* ndelay macro removed - never called */

void calibrate_delay(void);
void msleep(unsigned int msecs);
/* msleep_interruptible removed - never called */
static inline void ssleep(unsigned int seconds)
{
	msleep(seconds * 1000);
}

#endif  
