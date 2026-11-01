/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_MSR_H
#define _ASM_X86_MSR_H

#define DECLARE_ARGS(val, low, high) unsigned long long val
#define EAX_EDX_VAL(val, low, high) (val)
#define EAX_EDX_RET(val, low, high) "=A"(val)

/**
 * rdtsc() - returns the current TSC without ordering constraints
 *
 * rdtsc() returns the result of RDTSC as a 64-bit integer.  The
 * only ordering constraint it supplies is the ordering implied by
 * "asm volatile": it will put the RDTSC in the place you expect.  The
 * CPU can and will speculatively execute that RDTSC, though, so the
 * results can be non-monotonic if compared on different CPUs.
 */
static __always_inline unsigned long long rdtsc(void)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("rdtsc" : EAX_EDX_RET(val, low, high));

	return EAX_EDX_VAL(val, low, high);
}

/**
 * rdtsc_ordered() - read the current TSC in program order
 *
 * rdtsc_ordered() returns the result of RDTSC as a 64-bit integer.
 * It is ordered like a load to a global in-memory counter.  It should
 * be impossible to observe non-monotonic rdtsc_unordered() behavior
 * across multiple CPUs as long as the TSC is synced.
 */
static __always_inline unsigned long long rdtsc_ordered(void)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("rdtsc" : EAX_EDX_RET(val, low, high)::"ecx");

	return EAX_EDX_VAL(val, low, high);
}

#endif
