 
#ifndef _ASM_X86_SYNC_CORE_H
#define _ASM_X86_SYNC_CORE_H

#include <linux/preempt.h>
#include <asm/processor.h>
#include <asm/cpufeature.h>
#include <asm/special_insns.h>

static inline void iret_to_self(void)
{
	asm volatile (
		"pushfl\n\t"
		"pushl %%cs\n\t"
		"pushl $1f\n\t"
		"iret\n\t"
		"1:"
		: ASM_CALL_CONSTRAINT : : "memory");
}

 
static inline void sync_core(void)
{
	 
	if (static_cpu_has(X86_FEATURE_SERIALIZE)) {
		serialize();
		return;
	}

	 
	iret_to_self();
}

 
static inline void sync_core_before_usermode(void)
{
	 
	if (static_cpu_has(X86_FEATURE_PTI))
		return;

	 
	sync_core();
}

#endif  
