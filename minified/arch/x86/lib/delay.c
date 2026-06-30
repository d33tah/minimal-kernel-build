
#include <linux/sched.h>
#include <linux/timex.h>
#include <linux/preempt.h>
#include <linux/delay.h>

#include <asm/processor.h>
#include <asm/delay.h>
#include <asm/timer.h>


static void delay_loop(u64 __loops);

static void (*delay_fn)(u64) __ro_after_init = delay_loop;

static void delay_loop(u64 __loops)
{
	unsigned long loops = (unsigned long)__loops;

	asm volatile(
		"	test %0,%0	\n"
		"	jz 3f		\n"
		"	jmp 1f		\n"

		".align 16		\n"
		"1:	jmp 2f		\n"

		".align 16		\n"
		"2:	dec %0		\n"
		"	jnz 2b		\n"
		"3:	dec %0		\n"

		: "+a" (loops)
		:
	);
}

/*
 * TSC-based delay (delay_tsc) removed: it is only ever selected here via
 * use_tsc_delay(), a runtime optimization that is not required for correctness
 * (delay_fn permanently stays delay_loop, which is correct).  The helper is
 * kept as a no-op so the tsc_enable_sched_clock() callsite still links.
 */
void __init use_tsc_delay(void)
{
}

/*
 * TPAUSE-based delay (delay_halt/delay_halt_tpause) removed: it is only ever
 * selected here, and this is gated on X86_FEATURE_WAITPKG, which the boot CPU
 * does not have (runtime trace: use_tpause_delay never executes).  The helper
 * is kept as a no-op so the X86_FEATURE_WAITPKG callsite in arch/x86/kernel/
 * time.c still links.
 */
void __init use_tpause_delay(void)
{
}

void __delay(unsigned long loops)
{
	delay_fn(loops);
}

noinline void __const_udelay(unsigned long xloops)
{
	unsigned long lpj = this_cpu_read(cpu_info.loops_per_jiffy) ? : loops_per_jiffy;
	int d0;

	xloops *= 4;
	asm("mull %%edx"
		:"=d" (xloops), "=&a" (d0)
		:"1" (xloops), "0" (lpj * (HZ / 4)));

	__delay(++xloops);
}
