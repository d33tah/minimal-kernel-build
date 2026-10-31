
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

void __delay(unsigned long loops)
{
	delay_loop(loops);
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
