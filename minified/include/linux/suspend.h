/* Minimal suspend.h - stubs for !CONFIG_SUSPEND/!CONFIG_HIBERNATION */
#ifndef _LINUX_SUSPEND_H
#define _LINUX_SUSPEND_H

#include <linux/swap.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/mm.h>
#include <linux/freezer.h>
#include <asm/errno.h>

typedef int __bitwise suspend_state_t;

#define PM_SUSPEND_ON		((__force suspend_state_t) 0)
#define PM_SUSPEND_TO_IDLE	((__force suspend_state_t) 1)
#define PM_SUSPEND_STANDBY	((__force suspend_state_t) 2)
#define PM_SUSPEND_MEM		((__force suspend_state_t) 3)
#define PM_SUSPEND_MIN		PM_SUSPEND_TO_IDLE
#define PM_SUSPEND_MAX		((__force suspend_state_t) 4)

#define suspend_valid_only_mem	NULL

static inline bool idle_should_enter_s2idle(void) { return false; }

struct pbe {
	void *address;
	void *orig_address;
	struct pbe *next;
};

extern void mark_free_pages(struct zone *zone);

static inline void register_nosave_region(unsigned long b, unsigned long e) {}

static inline bool hibernation_available(void) { return false; }

extern struct mutex system_transition_mutex;

#define pm_notifier(fn, pri)	do { (void)(fn); } while (0)

#define pm_print_times_enabled	(false)
#define pm_debug_messages_on	(false)

#include <linux/printk.h>

#define __pm_pr_dbg(fmt, ...) \
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#define __pm_deferred_pr_dbg(fmt, ...) \
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#define pm_pr_dbg(fmt, ...) \
	__pm_pr_dbg(fmt, ##__VA_ARGS__)
#define pm_deferred_pr_dbg(fmt, ...) \
	__pm_deferred_pr_dbg(fmt, ##__VA_ARGS__)

#endif /* _LINUX_SUSPEND_H */
