 
#ifndef _ASM_X86_PVCLOCK_H
#define _ASM_X86_PVCLOCK_H

#include <asm/clocksource.h>
/* --- 2025-12-07 20:40 --- Inlined pvclock-abi.h */
struct pvclock_vcpu_time_info {
	u32   version;
	u32   pad0;
	u64   tsc_timestamp;
	u64   system_time;
	u32   tsc_to_system_mul;
	s8    tsc_shift;
	u8    flags;
	u8    pad[2];
} __attribute__((__packed__));
/* pvclock_wall_clock struct, pvclock_clocksource_read, pvclock_read_flags, pvclock_set_flags, pvclock_tsc_khz,
   pvclock_read_wallclock, pvclock_resume, pvclock_touch_watchdogs removed - unused */

/* pvclock_read_begin, pvclock_read_retry, pvclock_scale_delta, __pvclock_read_cycles,
   pvclock_vsyscall_time_info struct, pvclock_get_pvti_cpu0_va removed - unused */

#endif  
