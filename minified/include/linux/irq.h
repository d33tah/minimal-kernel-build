#ifndef _LINUX_IRQ_H
#define _LINUX_IRQ_H

#include <linux/cache.h>
struct irq_desc;
extern struct irq_desc *irq_to_desc(unsigned int irq);
extern int nr_irqs;
typedef void (*irq_flow_handler_t)(struct irq_desc *desc);
#include <linux/topology.h>
#include <asm/io.h>


struct module;

enum {
	IRQ_TYPE_NONE		= 0x00000000,
	IRQ_TYPE_EDGE_RISING	= 0x00000001,
	IRQ_TYPE_EDGE_FALLING	= 0x00000002,
	IRQ_TYPE_EDGE_BOTH	= (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING),
	IRQ_TYPE_LEVEL_HIGH	= 0x00000004,
	IRQ_TYPE_LEVEL_LOW	= 0x00000008,
	IRQ_TYPE_LEVEL_MASK	= (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH),
	IRQ_TYPE_SENSE_MASK	= 0x0000000f,

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
};

#define IRQF_MODIFY_MASK	\
	(IRQ_TYPE_SENSE_MASK | IRQ_NOPROBE | IRQ_NOREQUEST | \
	 IRQ_NOAUTOEN | IRQ_MOVE_PCNTXT | IRQ_LEVEL | IRQ_NO_BALANCING | \
	 IRQ_PER_CPU | IRQ_NESTED_THREAD | IRQ_NOTHREAD | IRQ_PER_CPU_DEVID | \
	 IRQ_IS_POLLED | IRQ_DISABLE_UNLAZY | IRQ_HIDDEN)

struct irq_common_data {
	unsigned int		__private state_use_accessors;
};

struct irq_data {
	unsigned int		irq;
	struct irq_common_data	*common;
	struct irq_chip		*chip;
};

enum {
	IRQD_ACTIVATED			= (1 <<  9),
	IRQD_IRQ_DISABLED		= (1 << 16),
	IRQD_IRQ_MASKED			= (1 << 17),
	IRQD_IRQ_INPROGRESS		= (1 << 18),
	IRQD_IRQ_STARTED		= (1 << 22),
};

#define __irqd_to_state(d) ACCESS_PRIVATE((d)->common, state_use_accessors)

static inline bool irqd_irq_disabled(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_IRQ_DISABLED;
}

static inline bool irqd_irq_masked(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_IRQ_MASKED;
}

static inline void irqd_set_activated(struct irq_data *d)
{
	__irqd_to_state(d) |= IRQD_ACTIVATED;
}

static inline bool irqd_is_started(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_IRQ_STARTED;
}

#undef __irqd_to_state

struct irq_chip {
	const char	*name;
	unsigned int	(*irq_startup)(struct irq_data *data);
	void		(*irq_enable)(struct irq_data *data);

	void		(*irq_ack)(struct irq_data *data);
	void		(*irq_mask)(struct irq_data *data);
	void		(*irq_mask_ack)(struct irq_data *data);
	void		(*irq_unmask)(struct irq_data *data);

	unsigned long	flags;
};

enum {
	IRQCHIP_SKIP_SET_WAKE			= (1 <<  4),
};

#include <linux/mutex.h>
struct module;
struct irq_desc {
	struct irq_common_data irq_common_data; struct irq_data irq_data;
	irq_flow_handler_t handle_irq;
	struct irqaction *action; unsigned int status_use_accessors;
	unsigned int core_internal_state__do_not_mess_with_it; unsigned int depth;
	raw_spinlock_t lock;
	unsigned long threads_oneshot;
	wait_queue_head_t wait_for_threads;
	struct mutex request_mutex; struct module *owner; const char *name;
} ____cacheline_internodealigned_in_smp;
static inline struct irq_data *irq_desc_get_irq_data(struct irq_desc *desc) { return &desc->irq_data; }
static inline void generic_handle_irq_desc(struct irq_desc *desc) { desc->handle_irq(desc); }


#ifndef ARCH_IRQ_INIT_FLAGS
# define ARCH_IRQ_INIT_FLAGS	0
#endif

#define IRQ_DEFAULT_INIT_FLAGS	ARCH_IRQ_INIT_FLAGS

struct irqaction;

extern void handle_level_irq(struct irq_desc *desc);
extern void handle_bad_irq(struct irq_desc *desc);

extern struct irq_chip no_irq_chip;

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
__irq_set_handler(unsigned int irq, irq_flow_handler_t handle,
		  const char *name);

#endif  
