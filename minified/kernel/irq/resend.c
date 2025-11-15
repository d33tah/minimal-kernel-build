 
 

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/interrupt.h>

#include "internals.h"


 
static DECLARE_BITMAP(irqs_resend, IRQ_BITMAP_BITS);

 
static void resend_irqs(struct tasklet_struct *unused)
{
	struct irq_desc *desc;
	int irq;

	while (!bitmap_empty(irqs_resend, nr_irqs)) {
		irq = find_first_bit(irqs_resend, nr_irqs);
		clear_bit(irq, irqs_resend);
		desc = irq_to_desc(irq);
		if (!desc)
			continue;
		local_irq_disable();
		desc->handle_irq(desc);
		local_irq_enable();
	}
}

 
static DECLARE_TASKLET(resend_tasklet, resend_irqs);

static int irq_sw_resend(struct irq_desc *desc)
{
	unsigned int irq = irq_desc_get_irq(desc);

	 
	if (handle_enforce_irqctx(&desc->irq_data))
		return -EINVAL;

	 
	if (irq_settings_is_nested_thread(desc)) {
		 
		if (!desc->parent_irq)
			return -EINVAL;
		irq = desc->parent_irq;
	}

	 
	set_bit(irq, irqs_resend);
	tasklet_schedule(&resend_tasklet);
	return 0;
}


static int try_retrigger(struct irq_desc *desc)
{
	if (desc->irq_data.chip->irq_retrigger)
		return desc->irq_data.chip->irq_retrigger(&desc->irq_data);

	return 0;
}

 
int check_irq_resend(struct irq_desc *desc, bool inject)
{
	int err = 0;

	 
	if (irq_settings_is_level(desc)) {
		desc->istate &= ~IRQS_PENDING;
		return -EINVAL;
	}

	if (desc->istate & IRQS_REPLAY)
		return -EBUSY;

	if (!(desc->istate & IRQS_PENDING) && !inject)
		return 0;

	desc->istate &= ~IRQS_PENDING;

	if (!try_retrigger(desc))
		err = irq_sw_resend(desc);

	 
	if (!err)
		desc->istate |= IRQS_REPLAY;
	return err;
}

