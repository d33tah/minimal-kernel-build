 
#ifndef _LINUX_IRQ_H
#define _LINUX_IRQ_H

 

#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/cpumask.h>
#include <linux/irqhandler.h>
#include <linux/irqreturn.h>
#include <linux/irqnr.h>
#include <linux/topology.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <asm/irq.h>
#include <asm/ptrace.h>
#include <asm/irq_regs.h>

struct seq_file;
struct module;
struct msi_msg;
struct irq_affinity_desc;
enum irqchip_irq_state;

 
enum {
	IRQ_TYPE_NONE		= 0x00000000,
	IRQ_TYPE_EDGE_RISING	= 0x00000001,
	IRQ_TYPE_EDGE_FALLING	= 0x00000002,
	IRQ_TYPE_EDGE_BOTH	= (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING),
	IRQ_TYPE_LEVEL_HIGH	= 0x00000004,
	IRQ_TYPE_LEVEL_LOW	= 0x00000008,
	IRQ_TYPE_LEVEL_MASK	= (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH),
	IRQ_TYPE_SENSE_MASK	= 0x0000000f,
	IRQ_TYPE_DEFAULT	= IRQ_TYPE_SENSE_MASK,

	IRQ_TYPE_PROBE		= 0x00000010,

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

#define IRQ_NO_BALANCING_MASK	(IRQ_PER_CPU | IRQ_NO_BALANCING)

 
enum {
	IRQ_SET_MASK_OK = 0,
	IRQ_SET_MASK_OK_NOCOPY,
	IRQ_SET_MASK_OK_DONE,
};

struct msi_desc;
struct irq_domain;

 
struct irq_common_data {
	unsigned int		__private state_use_accessors;
	void			*handler_data;
	struct msi_desc		*msi_desc;
	cpumask_var_t		affinity;
};

 
struct irq_data {
	u32			mask;
	unsigned int		irq;
	unsigned long		hwirq;
	struct irq_common_data	*common;
	struct irq_chip		*chip;
	struct irq_domain	*domain;
	void			*chip_data;
};

 
enum {
	IRQD_TRIGGER_MASK		= 0xf,
	IRQD_SETAFFINITY_PENDING	= (1 <<  8),
	IRQD_ACTIVATED			= (1 <<  9),
	IRQD_NO_BALANCING		= (1 << 10),
	IRQD_PER_CPU			= (1 << 11),
	IRQD_AFFINITY_SET		= (1 << 12),
	IRQD_LEVEL			= (1 << 13),
	IRQD_WAKEUP_STATE		= (1 << 14),
	IRQD_MOVE_PCNTXT		= (1 << 15),
	IRQD_IRQ_DISABLED		= (1 << 16),
	IRQD_IRQ_MASKED			= (1 << 17),
	IRQD_IRQ_INPROGRESS		= (1 << 18),
	IRQD_WAKEUP_ARMED		= (1 << 19),
	IRQD_FORWARDED_TO_VCPU		= (1 << 20),
	IRQD_AFFINITY_MANAGED		= (1 << 21),
	IRQD_IRQ_STARTED		= (1 << 22),
	IRQD_MANAGED_SHUTDOWN		= (1 << 23),
	IRQD_SINGLE_TARGET		= (1 << 24),
	IRQD_DEFAULT_TRIGGER_SET	= (1 << 25),
	IRQD_CAN_RESERVE		= (1 << 26),
	IRQD_MSI_NOMASK_QUIRK		= (1 << 27),
	IRQD_HANDLE_ENFORCE_IRQCTX	= (1 << 28),
	IRQD_AFFINITY_ON_ACTIVATE	= (1 << 29),
	IRQD_IRQ_ENABLED_ON_SUSPEND	= (1 << 30),
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

static inline bool irqd_affinity_is_managed(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_AFFINITY_MANAGED;
}

static inline bool irqd_is_activated(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_ACTIVATED;
}

static inline void irqd_set_activated(struct irq_data *d)
{
	__irqd_to_state(d) |= IRQD_ACTIVATED;
}

static inline void irqd_clr_activated(struct irq_data *d)
{
	__irqd_to_state(d) &= ~IRQD_ACTIVATED;
}

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

	void		(*irq_suspend)(struct irq_data *data);
	void		(*irq_resume)(struct irq_data *data);
	void		(*irq_pm_shutdown)(struct irq_data *data);

	void		(*irq_calc_mask)(struct irq_data *data);

	void		(*irq_print_chip)(struct irq_data *data, struct seq_file *p);
	int		(*irq_request_resources)(struct irq_data *data);
	void		(*irq_release_resources)(struct irq_data *data);

	void		(*irq_compose_msi_msg)(struct irq_data *data, struct msi_msg *msg);
	void		(*irq_write_msi_msg)(struct irq_data *data, struct msi_msg *msg);

	int		(*irq_get_irqchip_state)(struct irq_data *data, enum irqchip_irq_state which, bool *state);
	int		(*irq_set_irqchip_state)(struct irq_data *data, enum irqchip_irq_state which, bool state);

	int		(*irq_set_vcpu_affinity)(struct irq_data *data, void *vcpu_info);

	void		(*ipi_send_single)(struct irq_data *data, unsigned int cpu);
	void		(*ipi_send_mask)(struct irq_data *data, const struct cpumask *dest);

	int		(*irq_nmi_setup)(struct irq_data *data);
	void		(*irq_nmi_teardown)(struct irq_data *data);

	unsigned long	flags;
};

 
enum {
	IRQCHIP_SET_TYPE_MASKED			= (1 <<  0),
	IRQCHIP_EOI_IF_HANDLED			= (1 <<  1),
	IRQCHIP_MASK_ON_SUSPEND			= (1 <<  2),
	IRQCHIP_ONOFFLINE_ENABLED		= (1 <<  3),
	IRQCHIP_SKIP_SET_WAKE			= (1 <<  4),
	IRQCHIP_ONESHOT_SAFE			= (1 <<  5),
	IRQCHIP_EOI_THREADED			= (1 <<  6),
	IRQCHIP_SUPPORTS_LEVEL_MSI		= (1 <<  7),
	IRQCHIP_SUPPORTS_NMI			= (1 <<  8),
	IRQCHIP_ENABLE_WAKEUP_ON_SUSPEND	= (1 <<  9),
	IRQCHIP_AFFINITY_PRE_STARTUP		= (1 << 10),
	IRQCHIP_IMMUTABLE			= (1 << 11),
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
extern int setup_percpu_irq(unsigned int irq, struct irqaction *new);
extern void remove_percpu_irq(unsigned int irq, struct irqaction *act);

extern int irq_set_affinity_locked(struct irq_data *data,
				   const struct cpumask *cpumask, bool force);
extern int irq_set_vcpu_affinity(unsigned int irq, void *vcpu_info);

# define irq_affinity_online_cpu	NULL

extern int no_irq_affinity;

int irq_set_parent(int irq, int parent_irq);

 
extern void handle_level_irq(struct irq_desc *desc);
extern void handle_fasteoi_irq(struct irq_desc *desc);
extern void handle_edge_irq(struct irq_desc *desc);
extern void handle_edge_eoi_irq(struct irq_desc *desc);
extern void handle_simple_irq(struct irq_desc *desc);
extern void handle_untracked_irq(struct irq_desc *desc);
extern void handle_percpu_irq(struct irq_desc *desc);
extern void handle_percpu_devid_irq(struct irq_desc *desc);
extern void handle_bad_irq(struct irq_desc *desc);
extern void handle_nested_irq(unsigned int irq);

extern void handle_fasteoi_nmi(struct irq_desc *desc);
extern void handle_percpu_devid_fasteoi_nmi(struct irq_desc *desc);

extern int irq_chip_compose_msi_msg(struct irq_data *data, struct msi_msg *msg);
extern int irq_chip_pm_get(struct irq_data *data);
extern int irq_chip_pm_put(struct irq_data *data);

 
extern void note_interrupt(struct irq_desc *desc, irqreturn_t action_ret);


 
extern int noirqdebug_setup(char *str);

 
extern int can_request_irq(unsigned int irq, unsigned long irqflags);

 
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

extern int irq_set_percpu_devid(unsigned int irq);
extern int irq_set_percpu_devid_partition(unsigned int irq,
					  const struct cpumask *affinity);
extern int irq_get_percpu_devid_partition(unsigned int irq,
					  struct cpumask *affinity);

extern void
__irq_set_handler(unsigned int irq, irq_flow_handler_t handle, int is_chained,
		  const char *name);

void
irq_set_chained_handler_and_data(unsigned int irq, irq_flow_handler_t handle,
				 void *data);

void irq_modify_status(unsigned int irq, unsigned long clr, unsigned long set);

static inline void irq_set_status_flags(unsigned int irq, unsigned long set)
{
	irq_modify_status(irq, 0, set);
}

static inline void irq_set_percpu_devid_flags(unsigned int irq)
{
	irq_set_status_flags(irq,
			     IRQ_NOAUTOEN | IRQ_PER_CPU | IRQ_NOTHREAD |
			     IRQ_NOPROBE | IRQ_PER_CPU_DEVID);
}

 
extern int irq_set_chip(unsigned int irq, const struct irq_chip *chip);
extern int irq_set_handler_data(unsigned int irq, void *data);
extern int irq_set_chip_data(unsigned int irq, void *data);
extern int irq_set_irq_type(unsigned int irq, unsigned int type);
extern int irq_set_msi_desc(unsigned int irq, struct msi_desc *entry);
extern int irq_set_msi_desc_off(unsigned int irq_base, unsigned int irq_offset,
				struct msi_desc *entry);
extern struct irq_data *irq_get_irq_data(unsigned int irq);

static inline struct irq_chip *irq_data_get_irq_chip(struct irq_data *d)
{
	return d->chip;
}

static inline int irq_common_data_get_node(struct irq_common_data *d)
{
	return 0;
}

static inline struct cpumask *irq_data_get_affinity_mask(struct irq_data *d)
{
	return d->common->affinity;
}


unsigned int arch_dynirq_lower_bound(unsigned int from);

int __irq_alloc_descs(int irq, unsigned int from, unsigned int cnt, int node,
		      struct module *owner,
		      const struct irq_affinity_desc *affinity);

int __devm_irq_alloc_descs(struct device *dev, int irq, unsigned int from,
			   unsigned int cnt, int node, struct module *owner,
			   const struct irq_affinity_desc *affinity);

 
#define irq_alloc_descs(irq, from, cnt, node)	\
	__irq_alloc_descs(irq, from, cnt, node, THIS_MODULE, NULL)

#define irq_alloc_desc(node)			\
	irq_alloc_descs(-1, 1, 1, node)

#define irq_alloc_desc_at(at, node)		\
	irq_alloc_descs(at, at, 1, node)

#define irq_alloc_desc_from(from, node)		\
	irq_alloc_descs(-1, from, 1, node)

#define irq_alloc_descs_from(from, cnt, node)	\
	irq_alloc_descs(-1, from, cnt, node)

#define devm_irq_alloc_descs(dev, irq, from, cnt, node)		\
	__devm_irq_alloc_descs(dev, irq, from, cnt, node, THIS_MODULE, NULL)

#define devm_irq_alloc_desc(dev, node)				\
	devm_irq_alloc_descs(dev, -1, 1, 1, node)

#define devm_irq_alloc_desc_at(dev, at, node)			\
	devm_irq_alloc_descs(dev, at, at, 1, node)

#define devm_irq_alloc_desc_from(dev, from, node)		\
	devm_irq_alloc_descs(dev, -1, from, 1, node)

#define devm_irq_alloc_descs_from(dev, from, cnt, node)		\
	devm_irq_alloc_descs(dev, -1, from, cnt, node)

void irq_free_descs(unsigned int irq, unsigned int cnt);

struct irq_chip_generic;
struct irq_chip_type;
struct irq_chip_regs;
struct irq_domain_chip_generic;
enum irq_gc_flags { IRQ_GC_INIT_MASK_CACHE = 1 };


#define IRQ_MSK(n) (u32)((n) < 32 ? ((1 << (n)) - 1) : UINT_MAX)


#define irq_gc_lock_irqsave(gc, flags)	\
	raw_spin_lock_irqsave(&(gc)->lock, flags)

#define irq_gc_unlock_irqrestore(gc, flags)	\
	raw_spin_unlock_irqrestore(&(gc)->lock, flags)

struct irq_matrix;
struct irq_matrix *irq_alloc_matrix(unsigned int matrix_bits,
				    unsigned int alloc_start,
				    unsigned int alloc_end);
void irq_matrix_online(struct irq_matrix *m);
void irq_matrix_offline(struct irq_matrix *m);
void irq_matrix_assign_system(struct irq_matrix *m, unsigned int bit, bool replace);
int irq_matrix_reserve_managed(struct irq_matrix *m, const struct cpumask *msk);
void irq_matrix_remove_managed(struct irq_matrix *m, const struct cpumask *msk);
int irq_matrix_alloc_managed(struct irq_matrix *m, const struct cpumask *msk,
				unsigned int *mapped_cpu);
void irq_matrix_reserve(struct irq_matrix *m);
void irq_matrix_remove_reserved(struct irq_matrix *m);
int irq_matrix_alloc(struct irq_matrix *m, const struct cpumask *msk,
		     bool reserved, unsigned int *mapped_cpu);
void irq_matrix_free(struct irq_matrix *m, unsigned int cpu,
		     unsigned int bit, bool managed);
void irq_matrix_assign(struct irq_matrix *m, unsigned int bit);
unsigned int irq_matrix_available(struct irq_matrix *m, bool cpudown);
unsigned int irq_matrix_allocated(struct irq_matrix *m);
unsigned int irq_matrix_reserved(struct irq_matrix *m);
void irq_matrix_debug_show(struct seq_file *sf, struct irq_matrix *m, int ind);

 
#define INVALID_HWIRQ	(~0UL)
irq_hw_number_t ipi_get_hwirq(unsigned int irq, unsigned int cpu);
int __ipi_send_single(struct irq_desc *desc, unsigned int cpu);
int __ipi_send_mask(struct irq_desc *desc, const struct cpumask *dest);
int ipi_send_single(unsigned int virq, unsigned int cpu);
int ipi_send_mask(unsigned int virq, const struct cpumask *dest);

#ifndef set_handle_irq
#define set_handle_irq(handle_irq)		\
	do {					\
		(void)handle_irq;		\
		WARN_ON(1);			\
	} while (0)
#endif

#endif  
