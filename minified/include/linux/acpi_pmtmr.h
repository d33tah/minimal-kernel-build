/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ACPI_PMTMR_H_
#define _ACPI_PMTMR_H_

#include <linux/clocksource.h>

/* Number of PMTMR ticks expected during calibration run */
#define PMTMR_TICKS_PER_SEC 3579545

/* limit it to 24 bits */
#define ACPI_PM_MASK CLOCKSOURCE_MASK(24)

/* Overrun value */
#define ACPI_PM_OVRRUN	(1<<24)


static inline u32 acpi_pm_read_early(void)
{
	return 0;
}


#endif

