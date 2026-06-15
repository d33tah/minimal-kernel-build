 
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
struct pvclock_vsyscall_time_info {
	struct pvclock_vcpu_time_info pvti;
} __attribute__((__aligned__(SMP_CACHE_BYTES)));

static inline struct pvclock_vsyscall_time_info *pvclock_get_pvti_cpu0_va(void)
{
	return NULL;
}

#endif  
