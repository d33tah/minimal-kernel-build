// SPDX-License-Identifier: GPL-2.0
/*
 * Interrupt probing code - Stubbed minimal implementation
 */
#include <linux/irq.h>
#include <linux/module.h>

unsigned long probe_irq_on(void) { return 0; }
EXPORT_SYMBOL(probe_irq_on);

unsigned int probe_irq_mask(unsigned long val) { return 0; }
EXPORT_SYMBOL(probe_irq_mask);

int probe_irq_off(unsigned long val) { return 0; }
EXPORT_SYMBOL(probe_irq_off);
