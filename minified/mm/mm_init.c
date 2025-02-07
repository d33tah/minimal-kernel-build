// SPDX-License-Identifier: GPL-2.0-only
/*
 * mm_init.c - Memory initialisation verification and debugging
 *
 * Copyright 2008 IBM Corporation, 2008
 * Author Mel Gorman <mel@csn.ul.ie>
 *
 */
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/export.h>

#include "asm-generic/errno-base.h"


struct kobject *mm_kobj;
EXPORT_SYMBOL_GPL(mm_kobj);


static int __init mm_sysfs_init(void)
{
	mm_kobj = kobject_create_and_add("mm", kernel_kobj);
	if (!mm_kobj)
		return -ENOMEM;

	return 0;
}
postcore_initcall(mm_sysfs_init);
