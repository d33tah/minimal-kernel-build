/* Minimal suspend.h - stubs for !CONFIG_SUSPEND/!CONFIG_HIBERNATION */
#ifndef _LINUX_SUSPEND_H
#define _LINUX_SUSPEND_H
#include <linux/swap.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/mm.h>
/* linux/freezer.h removed - empty header */
#include <asm/errno.h>
/* suspend_state_t removed - unused */
struct pbe { void *address; void *orig_address; struct pbe *next; };
extern struct mutex system_transition_mutex;
#endif
