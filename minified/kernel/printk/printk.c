// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/kernel/printk.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * Modified to make sys_syslog() more flexible: added commands to
 * return the last 4k of kernel messages, regardless of whether
 * they've been read or not.  Added option to suppress kernel printk's
 * to the console.  Added hook for sending the console messages
 * elsewhere, in preparation for a serial line console (someday).
 * Ted Ts'o, 2/11/93.
 * Modified for sysctl support, 1/8/97, Chris Horn.
 * Fixed SMP synchronization, 08/08/99, Manfred Spraul
 *     manfred@colorfullife.com
 * Rewrote bits to get rid of console_lock
 *	01Mar01 Andrew Morton
 */

#include <linux/tty.h>
#include <linux/console.h>

/*
 * printk() is a no-op in this kernel (no log buffer), so nothing is ever
 * printed through a struct console. What remains is the console list that
 * /dev/console resolves through (console_device) and the console lock that
 * serialises the VT.
 */
static DEFINE_SEMAPHORE(console_sem);
static struct console *console_drivers;

/**
 * console_lock - lock the console system for exclusive use.
 *
 * Acquires a lock which guarantees that the caller has
 * exclusive access to the console system and the console_drivers list.
 *
 * Can sleep, returns nothing.
 */
void console_lock(void)
{
	down(&console_sem);
}

/**
 * console_unlock - unlock the console system
 *
 * Releases the console_lock which the caller holds on the console system
 * and the console driver list.
 *
 * While the console_lock was held, console output may have been buffered
 * by printk().  If this is the case, console_unlock(); emits
 * the output prior to releasing the lock.
 *
 * console_unlock(); may be called from any context.
 */
void console_unlock(void)
{
	up(&console_sem);
}

/*
 * Return the console tty driver structure and its associated index
 */
struct tty_driver *console_device(int *index)
{
	struct console *c;
	struct tty_driver *driver = NULL;

	console_lock();
	for (c = console_drivers; c && !driver; c = c->next)
		driver = c->device(c, index);
	console_unlock();
	return driver;
}

void register_console(struct console *newcon)
{
	struct console **p;

	console_lock();
	for (p = &console_drivers; *p; p = &(*p)->next)
		;
	newcon->next = NULL;
	*p = newcon;
	console_unlock();
}

/*
 * Initialize the console device. This is called *early*, so
 * we can't necessarily depend on lots of kernel help here.
 * Just do some early initializations, and do the complex setup
 * later.
 */
void __init console_init(void)
{
	initcall_entry_t *ce;

	for (ce = __con_initcall_start; ce < __con_initcall_end; ce++)
		initcall_from_entry(ce)();
}
