// SPDX-License-Identifier: GPL-2.0-only
/*
 * Touchscreen library - STUBBED for minimal kernel
 */

#include <linux/property.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/module.h>

/* Stub functions - do nothing */
void touchscreen_parse_properties(struct input_dev *input, bool multitouch,
				  struct touchscreen_properties *prop)
{
}
EXPORT_SYMBOL(touchscreen_parse_properties);

void touchscreen_set_mt_pos(struct input_mt_pos *pos,
			     const struct touchscreen_properties *prop,
			     unsigned int x, unsigned int y)
{
}
EXPORT_SYMBOL(touchscreen_set_mt_pos);

void touchscreen_report_pos(struct input_dev *input,
			     const struct touchscreen_properties *prop,
			     unsigned int x, unsigned int y,
			     bool multitouch)
{
}
EXPORT_SYMBOL(touchscreen_report_pos);
