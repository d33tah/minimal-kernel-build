
#ifndef _LINUX_TIMEX_H
#define _LINUX_TIMEX_H

#include <linux/time.h>

/* Inlined from uapi/linux/timex.h */
struct __kernel_timex_timeval {
	__kernel_time64_t       tv_sec;
	long long		tv_usec;
};

struct __kernel_timex {
	unsigned int modes;
	int :32;
	long long offset;
	long long freq;
	long long maxerror;
	long long esterror;
	int status;
	int :32;
	long long constant;
	long long precision;
	long long tolerance;
	struct __kernel_timex_timeval time;
	long long tick;
	long long ppsfreq;
	long long jitter;
	int shift;
	int :32;
	long long stabil;
	long long jitcnt;
	long long calcnt;
	long long errcnt;
	long long stbcnt;
	int tai;
	int  :32; int  :32; int  :32; int  :32;
	int  :32; int  :32; int  :32; int  :32;
	int  :32; int  :32; int  :32;
};
#define TIME_ERROR	5

#define ADJ_ADJTIME		0x8000 
#define ADJ_OFFSET_SINGLESHOT	0x0001	 
#define ADJ_OFFSET_READONLY	0x2000	 
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/param.h>

unsigned long random_get_entropy_fallback(void);

#include <asm/timex.h>

#ifndef random_get_entropy
#ifdef get_cycles
#define random_get_entropy()	((unsigned long)get_cycles())
#else
#define random_get_entropy()	random_get_entropy_fallback()
#endif
#endif

/* NTP tuning macros removed - unused:
   SHIFT_PLL, SHIFT_FLL, MAXTC, SHIFT_USEC, PPM_SCALE, PPM_SCALE_INV_SHIFT,
   PPM_SCALE_INV, MAXPHASE, MAXFREQ, MAXFREQ_SCALED, MINSEC, MAXSEC, NTP_PHASE_LIMIT */

extern unsigned long tick_usec;		 
extern unsigned long tick_nsec;		 

#define shift_right(x, s) ({	\
	__typeof__(x) __x = (x);	\
	__typeof__(s) __s = (s);	\
	__x < 0 ? -(-__x >> __s) : __x >> __s;	\
})

#define NTP_SCALE_SHIFT		32

#define NTP_INTERVAL_FREQ  (HZ)
#define NTP_INTERVAL_LENGTH (NSEC_PER_SEC/NTP_INTERVAL_FREQ)


#define PIT_TICK_RATE 1193182ul

#endif  
