// SPDX-License-Identifier: GPL-2.0
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/random.h>
#include <linux/module.h>

bool __do_once_start(bool *done, unsigned long *flags)
	__acquires(once_lock)
{
	return false;
}
EXPORT_SYMBOL(__do_once_start);

void __do_once_done(bool *done, struct static_key_true *once_key,
		    unsigned long *flags, struct module *mod)
	__releases(once_lock)
{
}
EXPORT_SYMBOL(__do_once_done);
