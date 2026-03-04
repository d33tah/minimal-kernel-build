 
#include <linux/irq.h>
#include <linux/smp.h>

# define IRQ_BITMAP_BITS	(NR_IRQS + 8196)

#define istate core_internal_state__do_not_mess_with_it

enum {
	IRQS_PENDING		= 0x00000200,
};

enum {
	_IRQ_DEFAULT_INIT_FLAGS	= IRQ_DEFAULT_INIT_FLAGS,
	_IRQ_NOAUTOEN		= IRQ_NOAUTOEN,
	_IRQF_MODIFY_MASK	= IRQF_MODIFY_MASK,
};

static inline void
irq_settings_clr_and_set(struct irq_desc *desc, u32 clr, u32 set)
{
	desc->status_use_accessors &= ~(clr & _IRQF_MODIFY_MASK);
	desc->status_use_accessors |= (set & _IRQF_MODIFY_MASK);
}

#define IRQ_RESEND	true
#define IRQ_START_COND	false

extern int irq_activate(struct irq_desc *desc);
extern int irq_startup(struct irq_desc *desc, bool resend, bool force);

extern void irq_enable(struct irq_desc *desc);

irqreturn_t handle_irq_event(struct irq_desc *desc);

static inline int check_irq_resend(struct irq_desc *desc, bool inject)
{
	desc->istate &= ~IRQS_PENDING;
	return 0;
}

static inline void chip_bus_lock(struct irq_desc *desc)
{
}

static inline void chip_bus_sync_unlock(struct irq_desc *desc)
{
}


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

static inline void irq_state_set_masked(struct irq_desc *desc)
{
	irqd_set(&desc->irq_data, IRQD_IRQ_MASKED);
}

#undef __irqd_to_state

static inline void kstat_incr_irqs_this_cpu(struct irq_desc *desc)
{
}

static inline int irq_domain_activate_irq(struct irq_data *data, bool reserve)
{
	irqd_set_activated(data);
	return 0;
}

