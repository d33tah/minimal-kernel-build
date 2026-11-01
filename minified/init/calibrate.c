// SPDX-License-Identifier: GPL-2.0
/* calibrate.c: default delay calibration
 *
 * Excised from init/main.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/delay.h>

unsigned long lpj_fine;

void calibrate_delay(void)
{
	/*
	 * MKB_NO_CALIBRATE: the delay loop is not calibrated. With a usable
	 * TSC, tsc_init() sets lpj_fine and it is used as upstream does.
	 * Without one, udelay()/mdelay() are uncalibrated and keep the default
	 * loops_per_jiffy of 4096, as before calibration upstream; nothing
	 * calls udelay() after init_IRQ().
	 */
	if (lpj_fine)
		loops_per_jiffy = lpj_fine;
}
