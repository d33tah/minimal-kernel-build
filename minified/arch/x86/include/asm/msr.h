 
#ifndef _ASM_X86_MSR_H
#define _ASM_X86_MSR_H

#define DECLARE_ARGS(val, low, high)	unsigned long long val
#define EAX_EDX_VAL(val, low, high)	(val)
#define EAX_EDX_RET(val, low, high)	"=A" (val)

 

/* do_trace_{read,write,rdpmc}_msr removed - tracepoints disabled, guards const-false */

 
/* __rdmsr / native_read_msr / rdmsr() removed - unused (no MSR reads in this build) */

static __always_inline unsigned long long rdtsc(void) {
	DECLARE_ARGS(val, low, high);

	asm volatile("rdtsc" : EAX_EDX_RET(val, low, high));

	return EAX_EDX_VAL(val, low, high); }

 
static __always_inline unsigned long long rdtsc_ordered(void) {
	DECLARE_ARGS(val, low, high);

	 
	asm volatile("rdtsc" : EAX_EDX_RET(val, low, high) :: "ecx");

	return EAX_EDX_VAL(val, low, high); }

 

#endif
