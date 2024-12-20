/* SPDX-License-Identifier: GPL-2.0 */
/*
 * IRQ subsystem internal functions and variables:
 *
 * Do not ever include this file from anything else than
 * kernel/irq/. Do not even think about using any information outside
 * of this file for your non core code.
 */
#include <linux/irqdesc.h>
#include <linux/kernel_stat.h>
#include <linux/pm_runtime.h>
#include <linux/sched/clock.h>

# define IRQ_BITMAP_BITS	(NR_IRQS + 8196)

#define istate core_internal_state__do_not_mess_with_it

extern bool noirqdebug;

extern struct irqaction chained_action;

/*
 * Bits used by threaded handlers:
 * IRQTF_RUNTHREAD - signals that the interrupt handler thread should run
 * IRQTF_WARNED    - warning "IRQ_WAKE_THREAD w/o thread_fn" has been printed
 * IRQTF_AFFINITY  - irq thread is requested to adjust affinity
 * IRQTF_FORCED_THREAD  - irq action is force threaded
 * IRQTF_READY     - signals that irq thread is ready
 */
enum {
	IRQTF_RUNTHREAD,
	IRQTF_WARNED,
	IRQTF_AFFINITY,
	IRQTF_FORCED_THREAD,
	IRQTF_READY,
};

/*
 * Bit masks for desc->core_internal_state__do_not_mess_with_it
 *
 * IRQS_AUTODETECT		- autodetection in progress
 * IRQS_SPURIOUS_DISABLED	- was disabled due to spurious interrupt
 *				  detection
 * IRQS_POLL_INPROGRESS		- polling in progress
 * IRQS_ONESHOT			- irq is not unmasked in primary handler
 * IRQS_REPLAY			- irq is replayed
 * IRQS_WAITING			- irq is waiting
 * IRQS_PENDING			- irq is pending and replayed later
 * IRQS_SUSPENDED		- irq is suspended
 * IRQS_NMI			- irq line is used to deliver NMIs
 */
enum {
	IRQS_AUTODETECT		= 0x00000001,
	IRQS_SPURIOUS_DISABLED	= 0x00000002,
	IRQS_POLL_INPROGRESS	= 0x00000008,
	IRQS_ONESHOT		= 0x00000020,
	IRQS_REPLAY		= 0x00000040,
	IRQS_WAITING		= 0x00000080,
	IRQS_PENDING		= 0x00000200,
	IRQS_SUSPENDED		= 0x00000800,
	IRQS_TIMINGS		= 0x00001000,
	IRQS_NMI		= 0x00002000,
};

#include "debug.h"
#include "settings.h"

extern int __irq_set_trigger(struct irq_desc *desc, unsigned long flags);
extern void __disable_irq(struct irq_desc *desc);
extern void __enable_irq(struct irq_desc *desc);

#define IRQ_RESEND	true
#define IRQ_NORESEND	false

#define IRQ_START_FORCE	true
#define IRQ_START_COND	false

extern int irq_activate(struct irq_desc *desc);
extern int irq_activate_and_startup(struct irq_desc *desc, bool resend);
extern int irq_startup(struct irq_desc *desc, bool resend, bool force);

extern void irq_shutdown(struct irq_desc *desc);
extern void irq_shutdown_and_deactivate(struct irq_desc *desc);
extern void irq_enable(struct irq_desc *desc);
extern void irq_disable(struct irq_desc *desc);
extern void irq_percpu_enable(struct irq_desc *desc, unsigned int cpu);
extern void irq_percpu_disable(struct irq_desc *desc, unsigned int cpu);
extern void mask_irq(struct irq_desc *desc);
extern void unmask_irq(struct irq_desc *desc);
extern void unmask_threaded_irq(struct irq_desc *desc);

static inline void irq_mark_irq(unsigned int irq) { }

extern int __irq_get_irqchip_state(struct irq_data *data,
				   enum irqchip_irq_state which,
				   bool *state);

extern void init_kstat_irqs(struct irq_desc *desc, int node, int nr);

irqreturn_t __handle_irq_event_percpu(struct irq_desc *desc);
irqreturn_t handle_irq_event_percpu(struct irq_desc *desc);
irqreturn_t handle_irq_event(struct irq_desc *desc);

/* Resending of interrupts :*/
int check_irq_resend(struct irq_desc *desc, bool inject);
bool irq_wait_for_poll(struct irq_desc *desc);
void __irq_wake_thread(struct irq_desc *desc, struct irqaction *action);

static inline void register_irq_proc(unsigned int irq, struct irq_desc *desc) { }
static inline void unregister_irq_proc(unsigned int irq, struct irq_desc *desc) { }
static inline void register_handler_proc(unsigned int irq,
					 struct irqaction *action) { }
static inline void unregister_handler_proc(unsigned int irq,
					   struct irqaction *action) { }

extern bool irq_can_set_affinity_usr(unsigned int irq);

extern void irq_set_thread_affinity(struct irq_desc *desc);

extern int irq_do_set_affinity(struct irq_data *data,
			       const struct cpumask *dest, bool force);

static inline int irq_setup_affinity(struct irq_desc *desc) { return 0; }

/* Inline functions for support of irq chips on slow busses */
static inline void chip_bus_lock(struct irq_desc *desc)
{
	if (unlikely(desc->irq_data.chip->irq_bus_lock))
		desc->irq_data.chip->irq_bus_lock(&desc->irq_data);
}

static inline void chip_bus_sync_unlock(struct irq_desc *desc)
{
	if (unlikely(desc->irq_data.chip->irq_bus_sync_unlock))
		desc->irq_data.chip->irq_bus_sync_unlock(&desc->irq_data);
}

#define _IRQ_DESC_CHECK		(1 << 0)
#define _IRQ_DESC_PERCPU	(1 << 1)

#define IRQ_GET_DESC_CHECK_GLOBAL	(_IRQ_DESC_CHECK)
#define IRQ_GET_DESC_CHECK_PERCPU	(_IRQ_DESC_CHECK | _IRQ_DESC_PERCPU)

#define for_each_action_of_desc(desc, act)			\
	for (act = desc->action; act; act = act->next)

struct irq_desc *
__irq_get_desc_lock(unsigned int irq, unsigned long *flags, bool bus,
		    unsigned int check);
void __irq_put_desc_unlock(struct irq_desc *desc, unsigned long flags, bool bus);

static inline struct irq_desc *
irq_get_desc_buslock(unsigned int irq, unsigned long *flags, unsigned int check)
{
	return __irq_get_desc_lock(irq, flags, true, check);
}

static inline void
irq_put_desc_busunlock(struct irq_desc *desc, unsigned long flags)
{
	__irq_put_desc_unlock(desc, flags, true);
}

static inline struct irq_desc *
irq_get_desc_lock(unsigned int irq, unsigned long *flags, unsigned int check)
{
	return __irq_get_desc_lock(irq, flags, false, check);
}

static inline void
irq_put_desc_unlock(struct irq_desc *desc, unsigned long flags)
{
	__irq_put_desc_unlock(desc, flags, false);
}

#define __irqd_to_state(d) ACCESS_PRIVATE((d)->common, state_use_accessors)

static inline unsigned int irqd_get(struct irq_data *d)
{
	return __irqd_to_state(d);
}

/*
 * Manipulation functions for irq_data.state
 */
static inline void irqd_set_move_pending(struct irq_data *d)
{
	__irqd_to_state(d) |= IRQD_SETAFFINITY_PENDING;
}

static inline void irqd_clr_move_pending(struct irq_data *d)
{
	__irqd_to_state(d) &= ~IRQD_SETAFFINITY_PENDING;
}

static inline void irqd_set_managed_shutdown(struct irq_data *d)
{
	__irqd_to_state(d) |= IRQD_MANAGED_SHUTDOWN;
}

static inline void irqd_clr_managed_shutdown(struct irq_data *d)
{
	__irqd_to_state(d) &= ~IRQD_MANAGED_SHUTDOWN;
}

static inline void irqd_clear(struct irq_data *d, unsigned int mask)
{
	__irqd_to_state(d) &= ~mask;
}

static inline void irqd_set(struct irq_data *d, unsigned int mask)
{
	__irqd_to_state(d) |= mask;
}

static inline bool irqd_has_set(struct irq_data *d, unsigned int mask)
{
	return __irqd_to_state(d) & mask;
}

static inline void irq_state_set_disabled(struct irq_desc *desc)
{
	irqd_set(&desc->irq_data, IRQD_IRQ_DISABLED);
}

static inline void irq_state_set_masked(struct irq_desc *desc)
{
	irqd_set(&desc->irq_data, IRQD_IRQ_MASKED);
}

#undef __irqd_to_state

static inline void __kstat_incr_irqs_this_cpu(struct irq_desc *desc)
{
	__this_cpu_inc(*desc->kstat_irqs);
	__this_cpu_inc(kstat.irqs_sum);
}

static inline void kstat_incr_irqs_this_cpu(struct irq_desc *desc)
{
	__kstat_incr_irqs_this_cpu(desc);
	desc->tot_count++;
}

static inline int irq_desc_get_node(struct irq_desc *desc)
{
	return irq_common_data_get_node(&desc->irq_common_data);
}

static inline int irq_desc_is_chained(struct irq_desc *desc)
{
	return (desc->action && desc->action == &chained_action);
}

static inline bool irq_pm_check_wakeup(struct irq_desc *desc) { return false; }
static inline void
irq_pm_install_action(struct irq_desc *desc, struct irqaction *action) { }
static inline void
irq_pm_remove_action(struct irq_desc *desc, struct irqaction *action) { }

static inline void irq_remove_timings(struct irq_desc *desc) {}
static inline void irq_setup_timings(struct irq_desc *desc,
				     struct irqaction *act) {};
static inline void record_irq_time(struct irq_desc *desc) {}


static inline void
irq_init_generic_chip(struct irq_chip_generic *gc, const char *name,
		      int num_ct, unsigned int irq_base,
		      void __iomem *reg_base, irq_flow_handler_t handler) { }

static inline bool irq_can_move_pcntxt(struct irq_data *data)
{
	return true;
}
static inline bool irq_move_pending(struct irq_data *data)
{
	return false;
}
static inline void
irq_copy_pending(struct irq_desc *desc, const struct cpumask *mask)
{
}
static inline void
irq_get_pending(struct cpumask *mask, struct irq_desc *desc)
{
}
static inline struct cpumask *irq_desc_get_pending_mask(struct irq_desc *desc)
{
	return NULL;
}
static inline bool irq_fixup_move_pending(struct irq_desc *desc, bool fclear)
{
	return false;
}
static inline bool handle_enforce_irqctx(struct irq_data *data)
{
	return false;
}

static inline int irq_domain_activate_irq(struct irq_data *data, bool reserve)
{
	irqd_set_activated(data);
	return 0;
}
static inline void irq_domain_deactivate_irq(struct irq_data *data)
{
	irqd_clr_activated(data);
}

static inline struct irq_data *irqd_get_parent_data(struct irq_data *irqd)
{
	return NULL;
}

static inline void irq_add_debugfs_entry(unsigned int irq, struct irq_desc *d)
{
}
static inline void irq_remove_debugfs_entry(struct irq_desc *d)
{
}
static inline void irq_debugfs_copy_devname(int irq, struct device *dev)
{
}
