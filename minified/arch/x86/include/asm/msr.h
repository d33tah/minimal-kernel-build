 
#ifndef _ASM_X86_MSR_H
#define _ASM_X86_MSR_H

#include "msr-index.h"

#ifndef __ASSEMBLY__

#include <asm/asm.h>
#include <asm/errno.h>
#include <asm/cpumask.h>
#include <uapi/asm/msr.h>
#include <asm/shared/msr.h>

struct msr_info {
	u32 msr_no;
	struct msr reg;
	struct msr *msrs;
	int err;
};

struct msr_regs_info {
	u32 *regs;
	int err;
};

struct saved_msr {
	bool valid;
	struct msr_info info;
};

struct saved_msrs {
	unsigned int num;
	struct saved_msr *array;
};

 
#define DECLARE_ARGS(val, low, high)	unsigned long long val
#define EAX_EDX_VAL(val, low, high)	(val)
#define EAX_EDX_RET(val, low, high)	"=A" (val)

 
#include <asm/atomic.h>
#include <linux/tracepoint-defs.h>

static inline void do_trace_write_msr(unsigned int msr, u64 val, int failed) {}
static inline void do_trace_read_msr(unsigned int msr, u64 val, int failed) {}
static inline void do_trace_rdpmc(unsigned int msr, u64 val, int failed) {}

 
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

#define native_rdmsr(msr, val1, val2)			\
do {							\
	u64 __val = __rdmsr((msr));			\
	(void)((val1) = (u32)__val);			\
	(void)((val2) = (u32)(__val >> 32));		\
} while (0)

#define native_wrmsr(msr, low, high)			\
	__wrmsr(msr, low, high)

#define native_wrmsrl(msr, val)				\
	__wrmsr((msr), (u32)((u64)(val)),		\
		       (u32)((u64)(val) >> 32))

static inline unsigned long long native_read_msr(unsigned int msr)
{
	unsigned long long val;

	val = __rdmsr(msr);

	if (tracepoint_enabled(read_msr))
		do_trace_read_msr(msr, val, 0);

	return val;
}

static inline unsigned long long native_read_msr_safe(unsigned int msr,
						      int *err)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("1: rdmsr ; xor %[err],%[err]\n"
		     "2:\n\t"
		     _ASM_EXTABLE_TYPE_REG(1b, 2b, EX_TYPE_RDMSR_SAFE, %[err])
		     : [err] "=r" (*err), EAX_EDX_RET(val, low, high)
		     : "c" (msr));
	if (tracepoint_enabled(read_msr))
		do_trace_read_msr(msr, EAX_EDX_VAL(val, low, high), *err);
	return EAX_EDX_VAL(val, low, high);
}

 
static inline void notrace
native_write_msr(unsigned int msr, u32 low, u32 high)
{
	__wrmsr(msr, low, high);

	if (tracepoint_enabled(write_msr))
		do_trace_write_msr(msr, ((u64)high << 32 | low), 0);
}

 
static inline int notrace
native_write_msr_safe(unsigned int msr, u32 low, u32 high)
{
	int err;

	asm volatile("1: wrmsr ; xor %[err],%[err]\n"
		     "2:\n\t"
		     _ASM_EXTABLE_TYPE_REG(1b, 2b, EX_TYPE_WRMSR_SAFE, %[err])
		     : [err] "=a" (err)
		     : "c" (msr), "0" (low), "d" (high)
		     : "memory");
	if (tracepoint_enabled(write_msr))
		do_trace_write_msr(msr, ((u64)high << 32 | low), err);
	return err;
}

extern int rdmsr_safe_regs(u32 regs[8]);
extern int wrmsr_safe_regs(u32 regs[8]);

 
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

 
static inline int wrmsr_safe(unsigned int msr, u32 low, u32 high)
{
	return native_write_msr_safe(msr, low, high);
}

 
#define rdmsr_safe(msr, low, high)				\
({								\
	int __err;						\
	u64 __val = native_read_msr_safe((msr), &__err);	\
	(*low) = (u32)__val;					\
	(*high) = (u32)(__val >> 32);				\
	__err;							\
})

static inline int rdmsrl_safe(unsigned int msr, unsigned long long *p)
{
	int err;

	*p = native_read_msr_safe(msr, &err);
	return err;
}

static inline int wrmsrl_safe(u32 msr, u64 val)
{
	return wrmsr_safe(msr, (u32)val,  (u32)(val >> 32));
}

struct msr *msrs_alloc(void);
void msrs_free(struct msr *msrs);
int msr_set_bit(u32 msr, u8 bit);
int msr_clear_bit(u32 msr, u8 bit);

#endif
#endif
