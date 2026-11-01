/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DELAY_H
#define _LINUX_DELAY_H

#include <linux/sched.h>

extern unsigned long loops_per_jiffy;
extern unsigned long lpj_fine;

#include <asm/delay.h>

#define MAX_UDELAY_MS 5

#define mdelay(n)                                            \
	((__builtin_constant_p(n) && (n) <= MAX_UDELAY_MS) ? \
		 udelay((n) * 1000) :                        \
		 ({                                          \
			 unsigned long __ms = (n);           \
			 while (__ms--)                      \
				 udelay(1000);               \
		 }))

void calibrate_delay(void);

#endif
