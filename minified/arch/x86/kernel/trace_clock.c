#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/barrier.h>
#include <asm/msr.h>

/* From asm/trace_clock.h */
#define ARCH_TRACE_CLOCKS \
	{ trace_clock_x86_tsc,	"x86-tsc",	.in_ns = 0 },

u64 notrace trace_clock_x86_tsc(void)
{
	return rdtsc_ordered();
}
