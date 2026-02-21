 
#ifndef _ASM_X86_MSR_H
#define _ASM_X86_MSR_H

/* msr-index.h inlined */
#include <linux/bits.h>
#define MSR_EFER		0xc0000080
#define _EFER_NX		11

#ifndef __ASSEMBLY__

#include <asm/asm.h>
#include <linux/cpumask.h>

#define DECLARE_ARGS(val, low, high)	unsigned long long val
#define EAX_EDX_VAL(val, low, high)	(val)
#define EAX_EDX_RET(val, low, high)	"=A" (val)

#include <asm/atomic.h>

static __always_inline unsigned long long rdtsc(void)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("rdtsc" : EAX_EDX_RET(val, low, high));

	return EAX_EDX_VAL(val, low, high);
}

static __always_inline unsigned long long rdtsc_ordered(void)
{
	DECLARE_ARGS(val, low, high);

	asm volatile(ALTERNATIVE_2("rdtsc",
				   "lfence; rdtsc", X86_FEATURE_LFENCE_RDTSC,
				   "rdtscp", X86_FEATURE_RDTSCP)
			: EAX_EDX_RET(val, low, high)
			 
			:: "ecx");

	return EAX_EDX_VAL(val, low, high);
}

#endif
#endif
