// SPDX-License-Identifier: GPL-2.0-only
/*
 *  i8042 keyboard and mouse controller driver for Linux - STUBBED (CONFIG_INPUT disabled)
 *
 *  Copyright (c) 1999-2004 Vojtech Pavlik
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/serio.h>
#include <linux/i8042.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("i8042 keyboard and mouse controller driver");
MODULE_LICENSE("GPL");

/* Stubbed functions */
void i8042_lock_chip(void)
{
}
EXPORT_SYMBOL(i8042_lock_chip);

void i8042_unlock_chip(void)
{
}
EXPORT_SYMBOL(i8042_unlock_chip);

int i8042_install_filter(bool (*filter)(unsigned char data, unsigned char str,
					struct serio *serio))
{
	return -ENODEV;
}
EXPORT_SYMBOL(i8042_install_filter);

int i8042_remove_filter(bool (*filter)(unsigned char data, unsigned char str,
				       struct serio *serio))
{
	return -ENODEV;
}
EXPORT_SYMBOL(i8042_remove_filter);

int i8042_command(unsigned char *param, int command)
{
	return -ENODEV;
}
EXPORT_SYMBOL(i8042_command);
