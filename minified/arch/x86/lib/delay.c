
#include <linux/delay.h>

#include <asm/timer.h>

static void delay_loop(u64 __loops);

static void (*delay_fn)(u64) __ro_after_init = delay_loop;

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

static void delay_tsc(u64 cycles)
{
	u64 bclock, now;
	int cpu;

	preempt_disable();
	cpu = smp_processor_id();
	bclock = rdtsc_ordered();
	for (;;) {
		now = rdtsc_ordered();
		if ((now - bclock) >= cycles)
			break;

		preempt_enable();
		rep_nop();
		preempt_disable();

		if (unlikely(cpu != smp_processor_id())) {
			cycles -= (now - bclock);
			cpu = smp_processor_id();
			bclock = rdtsc_ordered();
		}
	}
	preempt_enable();
}

void __init use_tsc_delay(void)
{
	if (delay_fn == delay_loop)
		delay_fn = delay_tsc;
}

void __delay(unsigned long loops)
{
	delay_fn(loops);
}

noinline void __const_udelay(unsigned long xloops)
{
	unsigned long lpj = this_cpu_read(cpu_info.loops_per_jiffy) ?:
				    loops_per_jiffy;
	int d0;

	xloops *= 4;
	asm("mull %%edx"
	    : "=d"(xloops), "=&a"(d0)
	    : "1"(xloops), "0"(lpj * (HZ / 4)));

	__delay(++xloops);
}

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x000010c7);
}
