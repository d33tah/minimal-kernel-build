// SPDX-License-Identifier: GPL-2.0
/*
 * Vivaldi function row keymap support - STUBBED for minimal kernel
 */

#include <linux/export.h>
#include <linux/input/vivaldi-fmap.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>

/* Stub function - do nothing */
int vivaldi_function_row_physmap_show(const struct vivaldi_data *data,
				      char *buf)
{
	return 0;
}
EXPORT_SYMBOL_GPL(vivaldi_function_row_physmap_show);
