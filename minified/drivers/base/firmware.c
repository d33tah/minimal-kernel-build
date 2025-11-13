// SPDX-License-Identifier: GPL-2.0
/*
 * firmware.c - firmware subsystem stub
 */

#include <linux/kobject.h>
#include <linux/export.h>

/* Stub kobject */
struct kobject *firmware_kobj;

/* Stub init function */
int __init firmware_init(void)
{
	return 0;
}
