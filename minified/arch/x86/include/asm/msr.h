 
#ifndef _ASM_X86_MSR_H
#define _ASM_X86_MSR_H

#include "msr-index.h"

#ifndef __ASSEMBLY__

#include <asm/asm.h>
#include <asm/errno.h>
#include <linux/cpumask.h>
struct msr {
	union {
		struct {
			u32 l;
			u32 h;
		};
		u64 q;
	};
};

#define DECLARE_ARGS(val, low, high)	unsigned long long val
#define EAX_EDX_VAL(val, low, high)	(val)
#define EAX_EDX_RET(val, low, high)	"=A" (val)

#include <asm/atomic.h>

static __always_inline unsigned long long __rdmsr(unsigned int msr)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("1: rdmsr\n"
		     "2:\n"
		     _ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_RDMSR)
		     : EAX_EDX_RET(val, low, high) : "c" (msr));

	return EAX_EDX_VAL(val, low, high);
}

static __always_inline void __wrmsr(unsigned int msr, u32 low, u32 high)
{
	asm volatile("1: wrmsr\n"
		     "2:\n"
		     _ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_WRMSR)
		     : : "c" (msr), "a"(low), "d" (high) : "memory");
}

static inline unsigned long long native_read_msr(unsigned int msr)
{
	return __rdmsr(msr);
}

static inline void notrace
native_write_msr(unsigned int msr, u32 low, u32 high)
{
	__wrmsr(msr, low, high);
}

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

#include <linux/errno.h>
 
#define rdmsr(msr, low, high)					\
do {								\
	u64 __val = native_read_msr((msr));			\
	(void)((low) = (u32)__val);				\
	(void)((high) = (u32)(__val >> 32));			\
} while (0)

static inline void wrmsr(unsigned int msr, u32 low, u32 high)
{
	native_write_msr(msr, low, high);
}

#define rdmsrl(msr, val)			\
	((val) = native_read_msr((msr)))

static inline void wrmsrl(unsigned int msr, u64 val)
{
	native_write_msr(msr, (u32)(val & 0xffffffffULL), (u32)(val >> 32));
}

#endif
#endif
