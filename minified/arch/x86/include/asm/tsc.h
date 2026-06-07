 
 
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
	if (!IS_ENABLED(CONFIG_X86_TSC) &&
	    !cpu_feature_enabled(X86_FEATURE_TSC))
		return 0;
	return rdtsc();
}
#define get_cycles get_cycles

/* convert_art_to_tsc, convert_art_ns_to_tsc removed - unused */

extern void tsc_early_init(void);
extern void tsc_init(void);
extern unsigned long calibrate_delay_is_known(void);
extern void mark_tsc_unstable(char *reason);
extern int unsynchronized_tsc(void);
/* check_tsc_unstable, mark_tsc_async_resets removed - unused */
extern unsigned long native_calibrate_cpu_early(void);
extern unsigned long native_calibrate_tsc(void);
extern unsigned long long native_sched_clock_from_tsc(u64 tsc);

extern int tsc_clocksource_reliable;
extern bool tsc_async_resets;

 
extern bool tsc_store_and_check_tsc_adjust(bool bootcpu);
extern void tsc_verify_tsc_adjust(bool resume);
/* check_tsc_sync_source, check_tsc_sync_target removed - unused (SMP) */

extern int notsc_setup(char *);
extern void tsc_save_sched_clock_state(void);
extern void tsc_restore_sched_clock_state(void);

unsigned long cpu_khz_from_msr(void);

#endif  
