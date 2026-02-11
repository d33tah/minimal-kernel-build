#ifndef _LINUX_TIMEX_H
#define _LINUX_TIMEX_H
#include <linux/time.h>
/* struct __kernel_timex_timeval, __kernel_timex removed - never used */
#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/param.h>
/* random_get_entropy_fallback declaration removed - function never called */
#include <asm/processor.h>
/* --- Inlined from asm/tsc.h --- */
#ifndef _ASM_X86_TSC_H
#define _ASM_X86_TSC_H
#include <asm/processor.h>
#include <asm/cpufeature.h>
typedef unsigned long long cycles_t;
static inline cycles_t get_cycles(void)
{
	return rdtsc();
}
#define get_cycles get_cycles
extern void tsc_early_init(void);
extern unsigned long native_calibrate_cpu_early(void);
extern unsigned long native_calibrate_tsc(void);
#endif /* _ASM_X86_TSC_H */
/* random_get_entropy macros removed - never called */
/* shift_right removed - never used */
#define NTP_SCALE_SHIFT		32
#define NTP_INTERVAL_FREQ  (HZ)
#define NTP_INTERVAL_LENGTH (NSEC_PER_SEC/NTP_INTERVAL_FREQ)
#define PIT_TICK_RATE 1193182ul
#endif
