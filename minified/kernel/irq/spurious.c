/* Stub: Spurious IRQ handling - simplified for minimal kernel */

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>

#include "internals.h"

bool noirqdebug __read_mostly;

/* Stub: irq_wait_for_poll - not needed for minimal kernel */
bool irq_wait_for_poll(struct irq_desc *desc)
{
	return false;
}

/* Stub: note_interrupt - spurious IRQ detection not needed for minimal kernel */
void note_interrupt(struct irq_desc *desc, irqreturn_t action_ret)
{
	/* Stub: no spurious IRQ tracking for minimal kernel */
}

int noirqdebug_setup(char *str)
{
	noirqdebug = 1;
	return 1;
}

__setup("noirqdebug", noirqdebug_setup);
module_param(noirqdebug, bool, 0644);
MODULE_PARM_DESC(noirqdebug, "Disable irq lockup detection when true");
