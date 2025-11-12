// SPDX-License-Identifier: GPL-2.0
/**
 * lib/minmax.c: stub implementation
 */
#include <linux/module.h>
#include <linux/win_minmax.h>

u32 minmax_running_max(struct minmax *m, u32 win, u32 t, u32 meas)
{
	return meas;
}
EXPORT_SYMBOL(minmax_running_max);
