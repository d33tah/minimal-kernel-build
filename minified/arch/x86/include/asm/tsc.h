 
 
#ifndef _ASM_X86_TSC_H
#define _ASM_X86_TSC_H

#include <asm/processor.h>
#include <asm/cpufeature.h>

 
typedef unsigned long long cycles_t;

extern unsigned int cpu_khz;
extern unsigned int tsc_khz;

/* disable_TSC removed - unused */

static inline cycles_t get_cycles(void)
{
	return rdtsc();
}
#define get_cycles get_cycles

/* convert_art_to_tsc, convert_art_ns_to_tsc removed - unused */

extern void tsc_early_init(void);
extern void tsc_init(void);
/* mark_tsc_unstable folded into tsc_init; check_tsc_unstable, mark_tsc_async_resets removed - unused */
extern unsigned long native_calibrate_cpu_early(void);
extern unsigned long native_calibrate_tsc(void);

extern int tsc_clocksource_reliable;
/* check_tsc_sync_source, check_tsc_sync_target removed - unused (SMP) */

#endif
