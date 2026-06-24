/* Minimal suspend.h - stubs for !CONFIG_SUSPEND/!CONFIG_HIBERNATION */
#ifndef _LINUX_SUSPEND_H
#define _LINUX_SUSPEND_H

#include <linux/swap.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/mm.h>
#include <asm/errno.h>

struct pbe {
	void *address;
	void *orig_address;
	struct pbe *next;
};

#endif /* _LINUX_SUSPEND_H */
