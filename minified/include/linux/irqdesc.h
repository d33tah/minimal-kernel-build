#ifndef _LINUX_IRQDESC_H
#define _LINUX_IRQDESC_H

#include <linux/rcupdate.h>
#include <linux/kobject.h>
#include <linux/mutex.h>


struct irq_desc {
	struct irq_common_data	irq_common_data;
	struct irq_data		irq_data;
	unsigned int __percpu	*kstat_irqs;
	irq_flow_handler_t	handle_irq;
	struct irqaction	*action;	 
	unsigned int		status_use_accessors;
	unsigned int		core_internal_state__do_not_mess_with_it;
	unsigned int		depth;
	unsigned int		irq_count;
	unsigned int		irqs_unhandled;
	raw_spinlock_t		lock;
	unsigned long		threads_oneshot;
	struct kobject		kobj;
	struct mutex		request_mutex;
} ____cacheline_internodealigned_in_smp;



static inline struct irq_desc *irq_data_to_desc(struct irq_data *data)
{
	return container_of(data->common, struct irq_desc, irq_common_data);
}

static inline unsigned int irq_desc_get_irq(struct irq_desc *desc)
{
	return desc->irq_data.irq;
}

static inline struct irq_data *irq_desc_get_irq_data(struct irq_desc *desc)
{
	return &desc->irq_data;
}


static inline void generic_handle_irq_desc(struct irq_desc *desc)
{
	desc->handle_irq(desc);
}




/* irq_set_handler_locked, irq_set_chip_handler_name_locked,
   irq_balancing_disabled, irq_is_percpu, irq_is_percpu_devid,
   irq_set_lockdep_class removed - unused */

#endif
