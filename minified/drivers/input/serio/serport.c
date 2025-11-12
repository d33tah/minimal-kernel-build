// SPDX-License-Identifier: GPL-2.0-only
/*
 * Serport support (serial port attached input devices) - STUBBED for minimal kernel
 */

#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/serio.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>

/* Empty stub - serport not needed for minimal kernel */
static int __init serport_init(void)
{
	return 0;
}

static void __exit serport_exit(void)
{
}

module_init(serport_init);
module_exit(serport_exit);

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Input device TTY line discipline");
MODULE_LICENSE("GPL");
