 
 
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
	/* CONFIG_X86_TSC=y, so TSC is always available */
	return rdtsc();
}
#define get_cycles get_cycles

/* convert_art_to_tsc, convert_art_ns_to_tsc removed - unused */

extern void tsc_early_init(void);
/* tsc_init removed - never called */
extern void mark_tsc_unstable(char *reason);
/* unsynchronized_tsc, check_tsc_unstable, mark_tsc_async_resets, tsc_async_resets removed - unused */
extern unsigned long native_calibrate_cpu_early(void);
extern unsigned long native_calibrate_tsc(void);
/* native_sched_clock_from_tsc removed - never called */
/* tsc_clocksource_reliable removed - never used */
/* tsc_store_and_check_tsc_adjust, tsc_verify_tsc_adjust, check_tsc_sync_source, check_tsc_sync_target removed - unused */

/* tsc_save/restore_sched_clock_state removed - never called */

unsigned long cpu_khz_from_msr(void);

#endif  
