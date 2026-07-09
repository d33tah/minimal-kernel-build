
#ifndef _LINUX_TIMEX_H
#define _LINUX_TIMEX_H

#include <linux/time.h>

/* Dead UABI structs __kernel_timex_timeval + __kernel_timex removed (0-ref, was a
   self-referential cluster: timeval only embedded by timex, timex only def + a bare
   forward decl in syscalls.h which is also removed). */
#include <linux/param.h>

#include <asm/timex.h>

/* NTP tuning macros removed - unused:
   SHIFT_PLL, SHIFT_FLL, MAXTC, SHIFT_USEC, PPM_SCALE, PPM_SCALE_INV_SHIFT,
   PPM_SCALE_INV, MAXPHASE, MAXFREQ, MAXFREQ_SCALED, MINSEC, MAXSEC, NTP_PHASE_LIMIT */


#define NTP_SCALE_SHIFT		32

#define NTP_INTERVAL_FREQ  (HZ)
#define NTP_INTERVAL_LENGTH (NSEC_PER_SEC/NTP_INTERVAL_FREQ)


#define PIT_TICK_RATE 1193182ul

#endif  
