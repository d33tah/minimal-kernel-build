/* Delay loop calibration */
#include <linux/delay.h>
#include <linux/jiffies.h>

/* Set by tsc.c when the TSC frequency was measured: TSC cycles per jiffy. */
unsigned long lpj_fine;

/*
 * Classic BogoMIPS search for __delay()'s loop: find the largest power of two
 * that still fits in one tick, then refine the lower bits by binary search.
 */
static unsigned long calibrate_delay_converge(void)
{
	unsigned long lpj = 1 << 12, bit, ticks;

	for (;;) {
		lpj <<= 1;
		if (!lpj)
			panic("delay calibration: timer tick does not advance");
		ticks = jiffies;
		while (ticks == jiffies)
			cpu_relax();
		ticks = jiffies;
		__delay(lpj);
		if (jiffies != ticks)
			break;
	}
	lpj >>= 1;
	for (bit = lpj >> 1; bit > lpj >> 8; bit >>= 1) {
		lpj |= bit;
		ticks = jiffies;
		while (ticks == jiffies)
			cpu_relax();
		ticks = jiffies;
		__delay(lpj);
		if (jiffies != ticks)
			lpj &= ~bit;
	}
	return lpj;
}

void calibrate_delay(void)
{
	loops_per_jiffy = lpj_fine ? lpj_fine : calibrate_delay_converge();
}
