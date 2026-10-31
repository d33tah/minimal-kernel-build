
#include <asm/param.h>
#include <linux/delay.h>

static void delay_loop(u64 __loops)
{
	unsigned long loops = (unsigned long)__loops;

	asm volatile("	test %0,%0	\n"
		     "	jz 3f		\n"
		     "	jmp 1f		\n"
		     ".align 16		\n"
		     "1:	jmp 2f		\n"
		     ".align 16		\n"
		     "2:	dec %0		\n"
		     "	jnz 2b		\n"
		     "3:	dec %0		\n"
		     : "+a"(loops)
		     :);
}

/* Busy-wait for a number of TSC cycles (loops_per_jiffy is then in cycles). */
static void delay_tsc(u64 cycles)
{
	u64 start = rdtsc_ordered();

	while (rdtsc_ordered() - start < cycles)
		cpu_relax();
}

static void (*delay_fn)(u64) __ro_after_init = delay_loop;

void __init use_tsc_delay(void)
{
	delay_fn = delay_tsc;
}

void __delay(unsigned long loops)
{
	delay_fn(loops);
}

noinline void __const_udelay(unsigned long xloops)
{
	unsigned long lpj = loops_per_jiffy;
	int d0;

	xloops *= 4;
	asm("mull %%edx"
	    : "=d"(xloops), "=&a"(d0)
	    : "1"(xloops), "0"(lpj * (HZ / 4)));

	__delay(++xloops);
}
