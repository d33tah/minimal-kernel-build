 
 
#include <linux/irqdesc.h>
#include <linux/kernel_stat.h>
#include <linux/pm_runtime.h>
#include <linux/sched/clock.h>

# define IRQ_BITMAP_BITS	(NR_IRQS + 8196)

#define istate core_internal_state__do_not_mess_with_it

extern bool noirqdebug;

extern struct irqaction chained_action;

 
enum {
	IRQTF_RUNTHREAD,
	IRQTF_WARNED,
	IRQTF_AFFINITY,
	IRQTF_FORCED_THREAD,
	IRQTF_READY,
};

 
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

/* --- 2025-12-08 02:25 --- Inlined from settings.h */
enum {
	_IRQ_DEFAULT_INIT_FLAGS	= IRQ_DEFAULT_INIT_FLAGS,
	_IRQ_PER_CPU		= IRQ_PER_CPU,
	_IRQ_LEVEL		= IRQ_LEVEL,
	_IRQ_NOPROBE		= IRQ_NOPROBE,
	_IRQ_NOREQUEST		= IRQ_NOREQUEST,
	_IRQ_NOTHREAD		= IRQ_NOTHREAD,
	_IRQ_NOAUTOEN		= IRQ_NOAUTOEN,
	_IRQ_MOVE_PCNTXT	= IRQ_MOVE_PCNTXT,
	_IRQ_NO_BALANCING	= IRQ_NO_BALANCING,
	_IRQ_NESTED_THREAD	= IRQ_NESTED_THREAD,
	_IRQ_PER_CPU_DEVID	= IRQ_PER_CPU_DEVID,
	_IRQ_IS_POLLED		= IRQ_IS_POLLED,
	_IRQ_DISABLE_UNLAZY	= IRQ_DISABLE_UNLAZY,
	_IRQ_HIDDEN		= IRQ_HIDDEN,
	_IRQ_NO_DEBUG		= IRQ_NO_DEBUG,
	_IRQF_MODIFY_MASK	= IRQF_MODIFY_MASK,
};

#define IRQ_PER_CPU		GOT_YOU_MORON
#define IRQ_NO_BALANCING	GOT_YOU_MORON
#define IRQ_LEVEL		GOT_YOU_MORON
#define IRQ_NOPROBE		GOT_YOU_MORON
#define IRQ_NOREQUEST		GOT_YOU_MORON
#define IRQ_NOTHREAD		GOT_YOU_MORON
#define IRQ_NOAUTOEN		GOT_YOU_MORON
#define IRQ_NESTED_THREAD	GOT_YOU_MORON
#define IRQ_PER_CPU_DEVID	GOT_YOU_MORON
#define IRQ_IS_POLLED		GOT_YOU_MORON
#define IRQ_DISABLE_UNLAZY	GOT_YOU_MORON
#define IRQ_HIDDEN		GOT_YOU_MORON
#define IRQ_NO_DEBUG		GOT_YOU_MORON
#undef IRQF_MODIFY_MASK
#define IRQF_MODIFY_MASK	GOT_YOU_MORON

static inline void
irq_settings_clr_and_set(struct irq_desc *desc, u32 clr, u32 set)
{
	desc->status_use_accessors &= ~(clr & _IRQF_MODIFY_MASK);
	desc->status_use_accessors |= (set & _IRQF_MODIFY_MASK);
}

static inline bool irq_settings_is_per_cpu(struct irq_desc *desc)
{
	return desc->status_use_accessors & _IRQ_PER_CPU;
}

static inline bool irq_settings_is_per_cpu_devid(struct irq_desc *desc)
{
	return desc->status_use_accessors & _IRQ_PER_CPU_DEVID;
}

static inline void irq_settings_set_per_cpu(struct irq_desc *desc)
{
	desc->status_use_accessors |= _IRQ_PER_CPU;
}

static inline void irq_settings_set_no_balancing(struct irq_desc *desc)
{
	desc->status_use_accessors |= _IRQ_NO_BALANCING;
}

/* irq_settings_has_no_balance_set, irq_settings_get_trigger_mask removed - unused */

static inline void
irq_settings_set_trigger_mask(struct irq_desc *desc, u32 mask)
{
	desc->status_use_accessors &= ~IRQ_TYPE_SENSE_MASK;
	desc->status_use_accessors |= mask & IRQ_TYPE_SENSE_MASK;
}

/* irq_settings_is_level removed - unused */

static inline void irq_settings_clr_level(struct irq_desc *desc)
{
	desc->status_use_accessors &= ~_IRQ_LEVEL;
}

static inline void irq_settings_set_level(struct irq_desc *desc)
{
	desc->status_use_accessors |= _IRQ_LEVEL;
}

static inline bool irq_settings_can_request(struct irq_desc *desc)
{
	return !(desc->status_use_accessors & _IRQ_NOREQUEST);
}

/* irq_settings_clr_norequest removed - unused */

static inline void irq_settings_set_norequest(struct irq_desc *desc)
{
	desc->status_use_accessors |= _IRQ_NOREQUEST;
}

static inline bool irq_settings_can_thread(struct irq_desc *desc)
{
	return !(desc->status_use_accessors & _IRQ_NOTHREAD);
}

/* irq_settings_clr_nothread removed - unused */

static inline void irq_settings_set_nothread(struct irq_desc *desc)
{
	desc->status_use_accessors |= _IRQ_NOTHREAD;
}

/* irq_settings_can_probe, irq_settings_clr_noprobe, irq_settings_can_move_pcntxt removed - unused */

static inline void irq_settings_set_noprobe(struct irq_desc *desc)
{
	desc->status_use_accessors |= _IRQ_NOPROBE;
}

static inline bool irq_settings_can_autoenable(struct irq_desc *desc)
{
	return !(desc->status_use_accessors & _IRQ_NOAUTOEN);
}

static inline bool irq_settings_is_nested_thread(struct irq_desc *desc)
{
	return desc->status_use_accessors & _IRQ_NESTED_THREAD;
}

/* irq_settings_is_polled removed - unused */

static inline bool irq_settings_disable_unlazy(struct irq_desc *desc)
{
	return desc->status_use_accessors & _IRQ_DISABLE_UNLAZY;
}

/* irq_settings_clr_disable_unlazy, irq_settings_is_hidden removed - unused */

static inline void irq_settings_set_no_debug(struct irq_desc *desc)
{
	desc->status_use_accessors |= _IRQ_NO_DEBUG;
}

/* irq_settings_no_debug removed - unused */

extern int __irq_set_trigger(struct irq_desc *desc, unsigned long flags);
/* __disable_irq removed - never called */
extern void __enable_irq(struct irq_desc *desc);

#define IRQ_RESEND	true
#define IRQ_START_FORCE	true
#define IRQ_START_COND	false
/* IRQ_NORESEND removed - unused */

extern int irq_activate(struct irq_desc *desc);
extern int irq_activate_and_startup(struct irq_desc *desc, bool resend);
extern int irq_startup(struct irq_desc *desc, bool resend, bool force);

extern void irq_shutdown(struct irq_desc *desc);
/* irq_shutdown_and_deactivate removed - never called */
extern void irq_enable(struct irq_desc *desc);
extern void irq_disable(struct irq_desc *desc);
extern void mask_irq(struct irq_desc *desc);
extern void unmask_irq(struct irq_desc *desc);
extern void unmask_threaded_irq(struct irq_desc *desc);

/* irq_mark_irq, __irq_get_irqchip_state, init_kstat_irqs removed - never called */

irqreturn_t __handle_irq_event_percpu(struct irq_desc *desc);
irqreturn_t handle_irq_event_percpu(struct irq_desc *desc);
irqreturn_t handle_irq_event(struct irq_desc *desc);

static inline int check_irq_resend(struct irq_desc *desc, bool inject)
{
	desc->istate &= ~IRQS_PENDING;
	return 0;
}
/* irq_wait_for_poll removed - never defined or called */
void __irq_wake_thread(struct irq_desc *desc, struct irqaction *action);

/* register_irq_proc, unregister_irq_proc, register_handler_proc,
   unregister_handler_proc removed - never called */

/* irq_can_set_affinity_usr, irq_set_thread_affinity, irq_do_set_affinity, irq_setup_affinity removed - unused */

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

/* irqd_get, irqd_set_move_pending, irqd_clr_move_pending, irqd_set_managed_shutdown, irqd_clr_managed_shutdown removed - unused */

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

/* __kstat_incr_irqs_this_cpu inlined into kstat_incr_irqs_this_cpu */

static inline void kstat_incr_irqs_this_cpu(struct irq_desc *desc)
{
	__this_cpu_inc(*desc->kstat_irqs);
	__this_cpu_inc(kstat.irqs_sum);
}

/* irq_desc_get_node, irq_desc_is_chained, irq_pm_check_wakeup, irq_pm_remove_action removed - unused */

/* irq_pm_install_action removed - never called */

/* irq_remove_timings, irq_setup_timings, record_irq_time removed - never called */


/* irq_init_generic_chip, irq_can_move_pcntxt, irq_move_pending,
   irq_copy_pending, irq_get_pending, irq_desc_get_pending_mask,
   irq_fixup_move_pending removed - unused */

static inline bool handle_enforce_irqctx(struct irq_data *data)
{
	return false;
}

static inline int irq_domain_activate_irq(struct irq_data *data, bool reserve)
{
	irqd_set_activated(data);
	return 0;
}
/* irq_domain_deactivate_irq, irqd_get_parent_data removed - unused */

/* irq_add_debugfs_entry, irq_remove_debugfs_entry,
   irq_debugfs_copy_devname removed - unused */
