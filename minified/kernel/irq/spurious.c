/* Stub: Spurious IRQ handling - simplified for minimal kernel */
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include "internals.h"
bool noirqdebug __read_mostly;
bool irq_wait_for_poll(struct irq_desc *desc)
{
	return false;
}
void note_interrupt(struct irq_desc *desc, irqreturn_t action_ret)
{
}
