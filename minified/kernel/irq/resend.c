/* Stub: IRQ resend - simplified for minimal kernel */

#include <linux/irq.h>
#include <linux/interrupt.h>

#include "internals.h"

/* Stub: check_irq_resend not needed for minimal kernel */
int check_irq_resend(struct irq_desc *desc, bool inject)
{
	/* Clear pending to prevent repeated calls */
	desc->istate &= ~IRQS_PENDING;
	return 0;
}

