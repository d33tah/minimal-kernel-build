#ifndef _LINUX_IRQ_H
#define _LINUX_IRQ_H


#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/cpumask.h>
struct irq_desc;
typedef void (*irq_flow_handler_t)(struct irq_desc *desc);
#include <linux/interrupt.h>
#include <linux/irqnr.h>
#include <linux/topology.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <asm/irq.h>
#include <asm/ptrace.h>
#include <asm/irq_regs.h>

struct module;
/* struct irq_affinity_desc removed - never used */

enum {
	IRQ_TYPE_NONE		= 0x00000000,
	IRQ_TYPE_EDGE_RISING	= 0x00000001,
	IRQ_TYPE_EDGE_FALLING	= 0x00000002,
	IRQ_TYPE_EDGE_BOTH	= (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING),
	IRQ_TYPE_LEVEL_HIGH	= 0x00000004,
	IRQ_TYPE_LEVEL_LOW	= 0x00000008,
	IRQ_TYPE_LEVEL_MASK	= (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH),
	IRQ_TYPE_SENSE_MASK	= 0x0000000f,
	/* IRQ_TYPE_DEFAULT, IRQ_TYPE_PROBE removed - never used */

	IRQ_LEVEL		= (1 <<  8),
	IRQ_PER_CPU		= (1 <<  9),
	IRQ_NOPROBE		= (1 << 10),
	IRQ_NOREQUEST		= (1 << 11),
	IRQ_NOAUTOEN		= (1 << 12),
	IRQ_NO_BALANCING	= (1 << 13),
	IRQ_MOVE_PCNTXT		= (1 << 14),
	IRQ_NESTED_THREAD	= (1 << 15),
	IRQ_NOTHREAD		= (1 << 16),
	IRQ_PER_CPU_DEVID	= (1 << 17),
	IRQ_IS_POLLED		= (1 << 18),
	IRQ_DISABLE_UNLAZY	= (1 << 19),
	IRQ_HIDDEN		= (1 << 20),
	IRQ_NO_DEBUG		= (1 << 21),
};

#define IRQF_MODIFY_MASK	\
	(IRQ_TYPE_SENSE_MASK | IRQ_NOPROBE | IRQ_NOREQUEST | \
	 IRQ_NOAUTOEN | IRQ_MOVE_PCNTXT | IRQ_LEVEL | IRQ_NO_BALANCING | \
	 IRQ_PER_CPU | IRQ_NESTED_THREAD | IRQ_NOTHREAD | IRQ_PER_CPU_DEVID | \
	 IRQ_IS_POLLED | IRQ_DISABLE_UNLAZY | IRQ_HIDDEN)

enum {
	IRQ_SET_MASK_OK = 0,
	IRQ_SET_MASK_OK_NOCOPY,
	IRQ_SET_MASK_OK_DONE,
};

/* struct msi_desc forward decl removed - msi_desc field removed */
struct irq_domain;

struct irq_common_data {
	unsigned int		__private state_use_accessors;
	/* handler_data removed - only set to NULL, never read */
	/* msi_desc removed - only set to NULL, never read */
	cpumask_var_t		affinity;
};

struct irq_data {
	/* mask removed - never accessed */
	unsigned int		irq;
	unsigned long		hwirq;
	struct irq_common_data	*common;
	struct irq_chip		*chip;
	struct irq_domain	*domain;
	void			*chip_data;
};

enum {
	IRQD_TRIGGER_MASK		= 0xf,
	/* IRQD_SETAFFINITY_PENDING removed - unused */
	IRQD_ACTIVATED			= (1 <<  9),
	IRQD_NO_BALANCING		= (1 << 10),
	IRQD_PER_CPU			= (1 << 11),
	/* IRQD_AFFINITY_SET removed - unused */
	IRQD_LEVEL			= (1 << 13),
	/* IRQD_WAKEUP_STATE, IRQD_MOVE_PCNTXT removed - unused */
	IRQD_IRQ_DISABLED		= (1 << 16),
	IRQD_IRQ_MASKED			= (1 << 17),
	IRQD_IRQ_INPROGRESS		= (1 << 18),
	IRQD_WAKEUP_ARMED		= (1 << 19),
	/* IRQD_FORWARDED_TO_VCPU, IRQD_AFFINITY_MANAGED, IRQD_MANAGED_SHUTDOWN removed - never used */
	IRQD_IRQ_STARTED		= (1 << 22),
	/* IRQD_SINGLE_TARGET removed - unused */
	IRQD_DEFAULT_TRIGGER_SET	= (1 << 25),
	/* IRQD_CAN_RESERVE, IRQD_MSI_NOMASK_QUIRK removed - unused */
	/* IRQD_HANDLE_ENFORCE_IRQCTX, IRQD_AFFINITY_ON_ACTIVATE removed - unused */
	/* IRQD_IRQ_ENABLED_ON_SUSPEND removed - unused */
};

#define __irqd_to_state(d) ACCESS_PRIVATE((d)->common, state_use_accessors)

static inline bool irqd_trigger_type_was_set(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_DEFAULT_TRIGGER_SET;
}

static inline u32 irqd_get_trigger_type(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_TRIGGER_MASK;
}

static inline void irqd_set_trigger_type(struct irq_data *d, u32 type)
{
	__irqd_to_state(d) &= ~IRQD_TRIGGER_MASK;
	__irqd_to_state(d) |= type & IRQD_TRIGGER_MASK;
	__irqd_to_state(d) |= IRQD_DEFAULT_TRIGGER_SET;
}

static inline bool irqd_irq_disabled(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_IRQ_DISABLED;
}

static inline bool irqd_irq_masked(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_IRQ_MASKED;
}

static inline bool irqd_irq_inprogress(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_IRQ_INPROGRESS;
}

/* irqd_affinity_is_managed removed - never called */

static inline bool irqd_is_activated(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_ACTIVATED;
}

static inline void irqd_set_activated(struct irq_data *d)
{
	__irqd_to_state(d) |= IRQD_ACTIVATED;
}

/* irqd_clr_activated removed - never called */

static inline bool irqd_is_started(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_IRQ_STARTED;
}

#undef __irqd_to_state

struct irq_chip {
	const char	*name;
	unsigned int	(*irq_startup)(struct irq_data *data);
	void		(*irq_shutdown)(struct irq_data *data);
	void		(*irq_enable)(struct irq_data *data);
	void		(*irq_disable)(struct irq_data *data);

	void		(*irq_ack)(struct irq_data *data);
	void		(*irq_mask)(struct irq_data *data);
	void		(*irq_mask_ack)(struct irq_data *data);
	void		(*irq_unmask)(struct irq_data *data);
	void		(*irq_eoi)(struct irq_data *data);

	int		(*irq_set_affinity)(struct irq_data *data, const struct cpumask *dest, bool force);
	int		(*irq_retrigger)(struct irq_data *data);
	int		(*irq_set_type)(struct irq_data *data, unsigned int flow_type);
	int		(*irq_set_wake)(struct irq_data *data, unsigned int on);

	void		(*irq_bus_lock)(struct irq_data *data);
	void		(*irq_bus_sync_unlock)(struct irq_data *data);
	/* irq_suspend, irq_resume, irq_pm_shutdown removed - never used */
	/* irq_calc_mask, irq_print_chip removed - never used */
	int		(*irq_request_resources)(struct irq_data *data);
	void		(*irq_release_resources)(struct irq_data *data);
	/* irq_compose_msi_msg, irq_write_msi_msg removed - never used */
	/* irq_get_irqchip_state, irq_set_irqchip_state removed - never used */
	/* irq_set_vcpu_affinity, ipi_send_*, irq_nmi_* removed - never used */
	unsigned long	flags;
};

enum {
	IRQCHIP_SET_TYPE_MASKED			= (1 <<  0),
	/* IRQCHIP_EOI_IF_HANDLED, IRQCHIP_MASK_ON_SUSPEND, IRQCHIP_ONOFFLINE_ENABLED removed - unused */
	IRQCHIP_SKIP_SET_WAKE			= (1 <<  4),
	IRQCHIP_ONESHOT_SAFE			= (1 <<  5),
	IRQCHIP_EOI_THREADED			= (1 <<  6),
	/* IRQCHIP_SUPPORTS_LEVEL_MSI, IRQCHIP_SUPPORTS_NMI, IRQCHIP_ENABLE_WAKEUP_ON_SUSPEND,
	   IRQCHIP_AFFINITY_PRE_STARTUP, IRQCHIP_IMMUTABLE removed - unused */
};

#include <linux/irqdesc.h>

#include <asm/hw_irq.h>

#ifndef NR_IRQS_LEGACY
# define NR_IRQS_LEGACY 0
#endif

#ifndef ARCH_IRQ_INIT_FLAGS
# define ARCH_IRQ_INIT_FLAGS	0
#endif

#define IRQ_DEFAULT_INIT_FLAGS	ARCH_IRQ_INIT_FLAGS

struct irqaction;

extern void handle_level_irq(struct irq_desc *desc);
extern void handle_bad_irq(struct irq_desc *desc);


/* irq_chip_pm_get/irq_chip_pm_put removed - always returned 0, no callers */
/* note_interrupt removed - declared but never called */

extern struct irq_chip no_irq_chip;
extern struct irq_chip dummy_irq_chip;

extern void
irq_set_chip_and_handler_name(unsigned int irq, const struct irq_chip *chip,
			      irq_flow_handler_t handle, const char *name);

static inline void irq_set_chip_and_handler(unsigned int irq,
					    const struct irq_chip *chip,
					    irq_flow_handler_t handle)
{
	irq_set_chip_and_handler_name(irq, chip, handle, NULL);
}


extern void
__irq_set_handler(unsigned int irq, irq_flow_handler_t handle, int is_chained,
		  const char *name);


/* irq_modify_status removed - never called */


extern int irq_set_chip(unsigned int irq, const struct irq_chip *chip);
/* irq_set_chip_data, irq_set_irq_type, irq_get_irq_data, irq_data_get_irq_chip, irq_data_get_affinity_mask, arch_dynirq_lower_bound removed - unused */

/* __irq_alloc_descs, irq_free_descs removed - never called */

#endif  
