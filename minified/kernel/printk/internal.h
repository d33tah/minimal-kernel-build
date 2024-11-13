/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * internal.h - printk internal definitions
 */
#include <linux/percpu.h>

#define printk_sysctl_init() do { } while (0)


/*
 * In !PRINTK builds we still export console_sem
 * semaphore and some of console functions (console_unlock()/etc.), so
 * printk-safe must preserve the existing local IRQ guarantees.
 */
#define printk_safe_enter_irqsave(flags) local_irq_save(flags)
#define printk_safe_exit_irqrestore(flags) local_irq_restore(flags)

static inline bool printk_percpu_data_ready(void) { return false; }
