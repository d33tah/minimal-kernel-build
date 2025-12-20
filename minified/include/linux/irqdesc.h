#ifndef _LINUX_IRQDESC_H
#define _LINUX_IRQDESC_H

#include <linux/rcupdate.h>
#include <linux/kobject.h>
#include <linux/mutex.h>


struct irq_affinity_notify;
struct proc_dir_entry;
struct module;
struct irq_desc;
struct irq_domain;
struct pt_regs;

struct irq_desc {
	struct irq_common_data	irq_common_data;
	struct irq_data		irq_data;
	unsigned int __percpu	*kstat_irqs;
	irq_flow_handler_t	handle_irq;
	struct irqaction	*action;	 
	unsigned int		status_use_accessors;
	unsigned int		core_internal_state__do_not_mess_with_it;
	unsigned int		depth;		 
	unsigned int		wake_depth;	 
	unsigned int		tot_count;
	unsigned int		irq_count;	 
	unsigned long		last_unhandled;	 
	unsigned int		irqs_unhandled;
	atomic_t		threads_handled;
	int			threads_handled_last;
	raw_spinlock_t		lock;
	struct cpumask		*percpu_enabled;
	const struct cpumask	*percpu_affinity;
	unsigned long		threads_oneshot;
	atomic_t		threads_active;
	wait_queue_head_t       wait_for_threads;
	struct rcu_head		rcu;
	struct kobject		kobj;
	struct mutex		request_mutex;
	int			parent_irq;
	struct module		*owner;
	const char		*name;
} ____cacheline_internodealigned_in_smp;

/* irq_lock_sparse, irq_unlock_sparse removed - never called */

/* irq_desc_kstat_cpu removed - never called */

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

/* irq_desc_get_chip removed - never called */
/* irq_desc_get_chip_data removed - never called */
/* irq_desc_get_handler_data removed - never called */

static inline void generic_handle_irq_desc(struct irq_desc *desc)
{
	desc->handle_irq(desc);
}

int handle_irq_desc(struct irq_desc *desc);
int generic_handle_irq(unsigned int irq);
int generic_handle_irq_safe(unsigned int irq);


/* irq_desc_has_action removed - never called */

/* irq_set_handler_locked, irq_set_chip_handler_name_locked,
   irq_balancing_disabled, irq_is_percpu, irq_is_percpu_devid,
   irq_set_lockdep_class removed - unused */

#endif
