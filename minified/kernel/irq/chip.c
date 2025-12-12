
#include <linux/irq.h>
#include <linux/module.h>

/* --- 2025-12-07 23:50 --- Inlined from linux/msi.h */
#include <linux/cpumask.h>
#include <linux/xarray.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <asm/hw_irq.h>
#include <linux/irqdomain.h>

/* --- 2025-12-08 02:10 --- Inlined from asm/msi.h */
static inline void x86_create_pci_msi_domain(void) { }
#define native_create_pci_msi_domain	NULL
#define x86_pci_msi_default_domain	NULL

typedef struct irq_alloc_info msi_alloc_info_t;

int pci_msi_prepare(struct irq_domain *domain, struct device *dev, int nvec,
		    msi_alloc_info_t *arg);

typedef struct x86_msi_data {
	union {
		struct {
			u32	vector			:  8,
				delivery_mode		:  3,
				dest_mode_logical	:  1,
				reserved		:  2,
				active_low		:  1,
				is_level		:  1;
		};
		u32	dmar_subhandle;
	};
} __attribute__ ((packed)) arch_msi_msg_data_t;
#define arch_msi_msg_data	x86_msi_data

typedef struct x86_msi_addr_lo {
	union {
		struct {
			u32	reserved_0		:  2,
				dest_mode_logical	:  1,
				redirect_hint		:  1,
				reserved_1		:  1,
				virt_destid_8_14	:  7,
				destid_0_7		:  8,
				base_address		: 12;
		};
		struct {
			u32	dmar_reserved_0		:  2,
				dmar_index_15		:  1,
				dmar_subhandle_valid	:  1,
				dmar_format		:  1,
				dmar_index_0_14		: 15,
				dmar_base_address	: 12;
		};
	};
} __attribute__ ((packed)) arch_msi_msg_addr_lo_t;
#define arch_msi_msg_addr_lo	x86_msi_addr_lo

#define X86_MSI_BASE_ADDRESS_LOW	(0xfee00000 >> 20)

typedef struct x86_msi_addr_hi {
	u32	reserved		:  8,
		destid_8_31		: 24;
} __attribute__ ((packed)) arch_msi_msg_addr_hi_t;
#define arch_msi_msg_addr_hi	x86_msi_addr_hi

#define X86_MSI_BASE_ADDRESS_HIGH	(0)

struct msi_msg;
u32 x86_msi_msg_get_destid(struct msi_msg *msg, bool extid);
/* --- End inlined asm/msi.h --- */

#ifndef arch_msi_msg_addr_lo
typedef struct arch_msi_msg_addr_lo {
	u32	address_lo;
} __attribute__ ((packed)) arch_msi_msg_addr_lo_t;
#endif

#ifndef arch_msi_msg_addr_hi
typedef struct arch_msi_msg_addr_hi {
	u32	address_hi;
} __attribute__ ((packed)) arch_msi_msg_addr_hi_t;
#endif

#ifndef arch_msi_msg_data
typedef struct arch_msi_msg_data {
	u32	data;
} __attribute__ ((packed)) arch_msi_msg_data_t;
#endif

struct msi_msg {
	union {
		u32			address_lo;
		arch_msi_msg_addr_lo_t	arch_addr_lo;
	};
	union {
		u32			address_hi;
		arch_msi_msg_addr_hi_t	arch_addr_hi;
	};
	union {
		u32			data;
		arch_msi_msg_data_t	arch_data;
	};
};

extern int pci_msi_ignore_mask;
struct irq_data;
struct pci_dev;
struct platform_msi_priv_data;
struct device_attribute;

typedef void (*irq_write_msi_msg_t)(struct msi_desc *desc,
				    struct msi_msg *msg);

struct pci_msi_desc {
	union {
		u32 msi_mask;
		u32 msix_ctrl;
	};
	struct {
		u8	is_msix		: 1;
		u8	multiple	: 3;
		u8	multi_cap	: 3;
		u8	can_mask	: 1;
		u8	is_64		: 1;
		u8	is_virtual	: 1;
		unsigned default_irq;
	} msi_attrib;
	union {
		u8	mask_pos;
		void __iomem *mask_base;
	};
};

#define MSI_MAX_INDEX		((unsigned int)USHRT_MAX)

struct msi_desc {
	unsigned int			irq;
	unsigned int			nvec_used;
	struct device			*dev;
	struct msi_msg			msg;
	struct irq_affinity_desc	*affinity;

	void (*write_msi_msg)(struct msi_desc *entry, void *data);
	void *write_msi_msg_data;

	u16				msi_index;
	struct pci_msi_desc		pci;
};

enum msi_desc_filter {
	MSI_DESC_ALL,
	MSI_DESC_NOTASSOCIATED,
	MSI_DESC_ASSOCIATED,
};

struct msi_device_data {
	unsigned long			properties;
	struct platform_msi_priv_data	*platform_data;
	struct mutex			mutex;
	struct xarray			__store;
	unsigned long			__iter_idx;
};

int msi_setup_device_data(struct device *dev);
unsigned int msi_get_virq(struct device *dev, unsigned int index);
void msi_lock_descs(struct device *dev);
void msi_unlock_descs(struct device *dev);
struct msi_desc *msi_first_desc(struct device *dev, enum msi_desc_filter filter);
struct msi_desc *msi_next_desc(struct device *dev, enum msi_desc_filter filter);

#define msi_for_each_desc(desc, dev, filter)			\
	for ((desc) = msi_first_desc((dev), (filter)); (desc);	\
	     (desc) = msi_next_desc((dev), (filter)))

#define msi_desc_to_dev(desc)		((desc)->dev)

static inline const void *msi_desc_get_iommu_cookie(struct msi_desc *desc)
{
	return NULL;
}

static inline void msi_desc_set_iommu_cookie(struct msi_desc *desc,
					     const void *iommu_cookie)
{
}

static inline void pci_write_msi_msg(unsigned int irq, struct msi_msg *msg)
{
}

int msi_add_msi_desc(struct device *dev, struct msi_desc *init_desc);
void msi_free_msi_descs_range(struct device *dev, enum msi_desc_filter filter,
			      unsigned int first_index, unsigned int last_index);

static inline void msi_free_msi_descs(struct device *dev)
{
	msi_free_msi_descs_range(dev, MSI_DESC_ALL, 0, MSI_MAX_INDEX);
}

void __pci_read_msi_msg(struct msi_desc *entry, struct msi_msg *msg);
void __pci_write_msi_msg(struct msi_desc *entry, struct msi_msg *msg);
void pci_msi_mask_irq(struct irq_data *data);
void pci_msi_unmask_irq(struct irq_data *data);
bool arch_restore_msi_irqs(struct pci_dev *dev);

static inline struct irq_domain *pci_msi_get_device_domain(struct pci_dev *pdev)
{
	return NULL;
}
/* end msi.h */
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/irqdomain.h>


#include "internals.h"

static irqreturn_t bad_chained_irq(int irq, void *dev_id)
{
	WARN_ONCE(1, "Chained irq %d should not call an action\n", irq);
	return IRQ_NONE;
}

struct irqaction chained_action = {
	.handler = bad_chained_irq,
};

int irq_set_chip(unsigned int irq, const struct irq_chip *chip)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_lock(irq, &flags, 0);

	if (!desc)
		return -EINVAL;

	desc->irq_data.chip = (struct irq_chip *)(chip ?: &no_irq_chip);
	irq_put_desc_unlock(desc, flags);
	 
	irq_mark_irq(irq);
	return 0;
}

int irq_set_irq_type(unsigned int irq, unsigned int type)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_buslock(irq, &flags, IRQ_GET_DESC_CHECK_GLOBAL);
	int ret = 0;

	if (!desc)
		return -EINVAL;

	ret = __irq_set_trigger(desc, type);
	irq_put_desc_busunlock(desc, flags);
	return ret;
}

int irq_set_chip_data(unsigned int irq, void *data)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_lock(irq, &flags, 0);

	if (!desc)
		return -EINVAL;
	desc->irq_data.chip_data = data;
	irq_put_desc_unlock(desc, flags);
	return 0;
}

struct irq_data *irq_get_irq_data(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	return desc ? &desc->irq_data : NULL;
}

static void irq_state_clr_disabled(struct irq_desc *desc)
{
	irqd_clear(&desc->irq_data, IRQD_IRQ_DISABLED);
}

static void irq_state_clr_masked(struct irq_desc *desc)
{
	irqd_clear(&desc->irq_data, IRQD_IRQ_MASKED);
}

static void irq_state_clr_started(struct irq_desc *desc)
{
	irqd_clear(&desc->irq_data, IRQD_IRQ_STARTED);
}

static void irq_state_set_started(struct irq_desc *desc)
{
	irqd_set(&desc->irq_data, IRQD_IRQ_STARTED);
}

enum {
	IRQ_STARTUP_NORMAL,
	IRQ_STARTUP_MANAGED,
	IRQ_STARTUP_ABORT,
};

static __always_inline int
__irq_startup_managed(struct irq_desc *desc, struct cpumask *aff, bool force)
{
	return IRQ_STARTUP_NORMAL;
}

static int __irq_startup(struct irq_desc *desc)
{
	struct irq_data *d = irq_desc_get_irq_data(desc);
	int ret = 0;

	 
	WARN_ON_ONCE(!irqd_is_activated(d));

	if (d->chip->irq_startup) {
		ret = d->chip->irq_startup(d);
		irq_state_clr_disabled(desc);
		irq_state_clr_masked(desc);
	} else {
		irq_enable(desc);
	}
	irq_state_set_started(desc);
	return ret;
}

int irq_startup(struct irq_desc *desc, bool resend, bool force)
{
	struct irq_data *d = irq_desc_get_irq_data(desc);
	struct cpumask *aff = irq_data_get_affinity_mask(d);
	int ret = 0;

	desc->depth = 0;

	if (irqd_is_started(d)) {
		irq_enable(desc);
	} else {
		switch (__irq_startup_managed(desc, aff, force)) {
		case IRQ_STARTUP_NORMAL:
			if (d->chip->flags & IRQCHIP_AFFINITY_PRE_STARTUP)
				irq_setup_affinity(desc);
			ret = __irq_startup(desc);
			if (!(d->chip->flags & IRQCHIP_AFFINITY_PRE_STARTUP))
				irq_setup_affinity(desc);
			break;
		case IRQ_STARTUP_MANAGED:
			irq_do_set_affinity(d, aff, false);
			ret = __irq_startup(desc);
			break;
		case IRQ_STARTUP_ABORT:
			irqd_set_managed_shutdown(d);
			return 0;
		}
	}
	if (resend)
		check_irq_resend(desc, false);

	return ret;
}

int irq_activate(struct irq_desc *desc)
{
	struct irq_data *d = irq_desc_get_irq_data(desc);

	if (!irqd_affinity_is_managed(d))
		return irq_domain_activate_irq(d, false);
	return 0;
}

int irq_activate_and_startup(struct irq_desc *desc, bool resend)
{
	if (WARN_ON(irq_activate(desc)))
		return 0;
	return irq_startup(desc, resend, IRQ_START_FORCE);
}

static void __irq_disable(struct irq_desc *desc, bool mask);

void irq_shutdown(struct irq_desc *desc)
{
	if (irqd_is_started(&desc->irq_data)) {
		desc->depth = 1;
		if (desc->irq_data.chip->irq_shutdown) {
			desc->irq_data.chip->irq_shutdown(&desc->irq_data);
			irq_state_set_disabled(desc);
			irq_state_set_masked(desc);
		} else {
			__irq_disable(desc, true);
		}
		irq_state_clr_started(desc);
	}
}


void irq_shutdown_and_deactivate(struct irq_desc *desc)
{
	irq_shutdown(desc);
	 
	irq_domain_deactivate_irq(&desc->irq_data);
}

void irq_enable(struct irq_desc *desc)
{
	if (!irqd_irq_disabled(&desc->irq_data)) {
		unmask_irq(desc);
	} else {
		irq_state_clr_disabled(desc);
		if (desc->irq_data.chip->irq_enable) {
			desc->irq_data.chip->irq_enable(&desc->irq_data);
			irq_state_clr_masked(desc);
		} else {
			unmask_irq(desc);
		}
	}
}

static void __irq_disable(struct irq_desc *desc, bool mask)
{
	if (irqd_irq_disabled(&desc->irq_data)) {
		if (mask)
			mask_irq(desc);
	} else {
		irq_state_set_disabled(desc);
		if (desc->irq_data.chip->irq_disable) {
			desc->irq_data.chip->irq_disable(&desc->irq_data);
			irq_state_set_masked(desc);
		} else if (mask) {
			mask_irq(desc);
		}
	}
}

void irq_disable(struct irq_desc *desc)
{
	__irq_disable(desc, irq_settings_disable_unlazy(desc));
}

static void irq_percpu_disable(struct irq_desc *desc, unsigned int cpu)
{
	if (desc->irq_data.chip->irq_disable)
		desc->irq_data.chip->irq_disable(&desc->irq_data);
	else
		desc->irq_data.chip->irq_mask(&desc->irq_data);
	cpumask_clear_cpu(cpu, desc->percpu_enabled);
}

static inline void mask_ack_irq(struct irq_desc *desc)
{
	if (desc->irq_data.chip->irq_mask_ack) {
		desc->irq_data.chip->irq_mask_ack(&desc->irq_data);
		irq_state_set_masked(desc);
	} else {
		mask_irq(desc);
		if (desc->irq_data.chip->irq_ack)
			desc->irq_data.chip->irq_ack(&desc->irq_data);
	}
}

void mask_irq(struct irq_desc *desc)
{
	if (irqd_irq_masked(&desc->irq_data))
		return;

	if (desc->irq_data.chip->irq_mask) {
		desc->irq_data.chip->irq_mask(&desc->irq_data);
		irq_state_set_masked(desc);
	}
}

void unmask_irq(struct irq_desc *desc)
{
	if (!irqd_irq_masked(&desc->irq_data))
		return;

	if (desc->irq_data.chip->irq_unmask) {
		desc->irq_data.chip->irq_unmask(&desc->irq_data);
		irq_state_clr_masked(desc);
	}
}

void unmask_threaded_irq(struct irq_desc *desc)
{
	struct irq_chip *chip = desc->irq_data.chip;

	if (chip->flags & IRQCHIP_EOI_THREADED)
		chip->irq_eoi(&desc->irq_data);

	unmask_irq(desc);
}

static bool irq_check_poll(struct irq_desc *desc)
{
	if (!(desc->istate & IRQS_POLL_INPROGRESS))
		return false;
	return irq_wait_for_poll(desc);
}

static bool irq_may_run(struct irq_desc *desc)
{
	unsigned int mask = IRQD_IRQ_INPROGRESS | IRQD_WAKEUP_ARMED;

	 
	if (!irqd_has_set(&desc->irq_data, mask))
		return true;

	 
	if (irq_pm_check_wakeup(desc))
		return false;

	 
	return irq_check_poll(desc);
}

void handle_simple_irq(struct irq_desc *desc)
{
	raw_spin_lock(&desc->lock);

	if (!irq_may_run(desc))
		goto out_unlock;

	desc->istate &= ~(IRQS_REPLAY | IRQS_WAITING);

	if (unlikely(!desc->action || irqd_irq_disabled(&desc->irq_data))) {
		desc->istate |= IRQS_PENDING;
		goto out_unlock;
	}

	kstat_incr_irqs_this_cpu(desc);
	handle_irq_event(desc);

out_unlock:
	raw_spin_unlock(&desc->lock);
}

/* handle_untracked_irq removed - unused (~26 LOC) */

static void cond_unmask_irq(struct irq_desc *desc)
{
	 
	if (!irqd_irq_disabled(&desc->irq_data) &&
	    irqd_irq_masked(&desc->irq_data) && !desc->threads_oneshot)
		unmask_irq(desc);
}

void handle_level_irq(struct irq_desc *desc)
{
	raw_spin_lock(&desc->lock);
	mask_ack_irq(desc);

	if (!irq_may_run(desc))
		goto out_unlock;

	desc->istate &= ~(IRQS_REPLAY | IRQS_WAITING);

	 
	if (unlikely(!desc->action || irqd_irq_disabled(&desc->irq_data))) {
		desc->istate |= IRQS_PENDING;
		goto out_unlock;
	}

	kstat_incr_irqs_this_cpu(desc);
	handle_irq_event(desc);

	cond_unmask_irq(desc);

out_unlock:
	raw_spin_unlock(&desc->lock);
}

static void cond_unmask_eoi_irq(struct irq_desc *desc, struct irq_chip *chip)
{
	if (!(desc->istate & IRQS_ONESHOT)) {
		chip->irq_eoi(&desc->irq_data);
		return;
	}
	 
	if (!irqd_irq_disabled(&desc->irq_data) &&
	    irqd_irq_masked(&desc->irq_data) && !desc->threads_oneshot) {
		chip->irq_eoi(&desc->irq_data);
		unmask_irq(desc);
	} else if (!(chip->flags & IRQCHIP_EOI_THREADED)) {
		chip->irq_eoi(&desc->irq_data);
	}
}

void handle_fasteoi_irq(struct irq_desc *desc)
{
	struct irq_chip *chip = desc->irq_data.chip;

	raw_spin_lock(&desc->lock);

	if (!irq_may_run(desc))
		goto out;

	desc->istate &= ~(IRQS_REPLAY | IRQS_WAITING);

	 
	if (unlikely(!desc->action || irqd_irq_disabled(&desc->irq_data))) {
		desc->istate |= IRQS_PENDING;
		mask_irq(desc);
		goto out;
	}

	kstat_incr_irqs_this_cpu(desc);
	if (desc->istate & IRQS_ONESHOT)
		mask_irq(desc);

	handle_irq_event(desc);

	cond_unmask_eoi_irq(desc, chip);

	raw_spin_unlock(&desc->lock);
	return;
out:
	if (!(chip->flags & IRQCHIP_EOI_IF_HANDLED))
		chip->irq_eoi(&desc->irq_data);
	raw_spin_unlock(&desc->lock);
}

/* handle_fasteoi_nmi removed - unused */

void handle_edge_irq(struct irq_desc *desc)
{
	raw_spin_lock(&desc->lock);

	desc->istate &= ~(IRQS_REPLAY | IRQS_WAITING);

	if (!irq_may_run(desc)) {
		desc->istate |= IRQS_PENDING;
		mask_ack_irq(desc);
		goto out_unlock;
	}

	 
	if (irqd_irq_disabled(&desc->irq_data) || !desc->action) {
		desc->istate |= IRQS_PENDING;
		mask_ack_irq(desc);
		goto out_unlock;
	}

	kstat_incr_irqs_this_cpu(desc);

	 
	desc->irq_data.chip->irq_ack(&desc->irq_data);

	do {
		if (unlikely(!desc->action)) {
			mask_irq(desc);
			goto out_unlock;
		}

		 
		if (unlikely(desc->istate & IRQS_PENDING)) {
			if (!irqd_irq_disabled(&desc->irq_data) &&
			    irqd_irq_masked(&desc->irq_data))
				unmask_irq(desc);
		}

		handle_irq_event(desc);

	} while ((desc->istate & IRQS_PENDING) &&
		 !irqd_irq_disabled(&desc->irq_data));

out_unlock:
	raw_spin_unlock(&desc->lock);
}


void handle_percpu_irq(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);

	 
	__kstat_incr_irqs_this_cpu(desc);

	if (chip->irq_ack)
		chip->irq_ack(&desc->irq_data);

	handle_irq_event_percpu(desc);

	if (chip->irq_eoi)
		chip->irq_eoi(&desc->irq_data);
}

void handle_percpu_devid_irq(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irqaction *action = desc->action;
	unsigned int irq = irq_desc_get_irq(desc);
	irqreturn_t res;

	 
	__kstat_incr_irqs_this_cpu(desc);

	if (chip->irq_ack)
		chip->irq_ack(&desc->irq_data);

	if (likely(action)) {
		 
		res = action->handler(irq, raw_cpu_ptr(action->percpu_dev_id));
		 
	} else {
		unsigned int cpu = smp_processor_id();
		bool enabled = cpumask_test_cpu(cpu, desc->percpu_enabled);

		if (enabled)
			irq_percpu_disable(desc, cpu);

		pr_err_once("Spurious%s percpu IRQ%u on CPU%u\n",
			    enabled ? " and unmasked" : "", irq, cpu);
	}

	if (chip->irq_eoi)
		chip->irq_eoi(&desc->irq_data);
}

/* handle_percpu_devid_fasteoi_nmi removed - unused */

static void
__irq_do_set_handler(struct irq_desc *desc, irq_flow_handler_t handle,
		     int is_chained, const char *name)
{
	if (!handle) {
		handle = handle_bad_irq;
	} else {
		struct irq_data *irq_data = &desc->irq_data;
		if (WARN_ON(!irq_data || irq_data->chip == &no_irq_chip))
			return;
	}

	 
	if (handle == handle_bad_irq) {
		if (desc->irq_data.chip != &no_irq_chip)
			mask_ack_irq(desc);
		irq_state_set_disabled(desc);
		if (is_chained) {
			desc->action = NULL;
			WARN_ON(irq_chip_pm_put(irq_desc_get_irq_data(desc)));
		}
		desc->depth = 1;
	}
	desc->handle_irq = handle;
	desc->name = name;

	if (handle != handle_bad_irq && is_chained) {
		unsigned int type = irqd_get_trigger_type(&desc->irq_data);

		 
		if (type != IRQ_TYPE_NONE) {
			__irq_set_trigger(desc, type);
			desc->handle_irq = handle;
		}

		irq_settings_set_noprobe(desc);
		irq_settings_set_norequest(desc);
		irq_settings_set_nothread(desc);
		desc->action = &chained_action;
		WARN_ON(irq_chip_pm_get(irq_desc_get_irq_data(desc)));
		irq_activate_and_startup(desc, IRQ_RESEND);
	}
}

void
__irq_set_handler(unsigned int irq, irq_flow_handler_t handle, int is_chained,
		  const char *name)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_buslock(irq, &flags, 0);

	if (!desc)
		return;

	__irq_do_set_handler(desc, handle, is_chained, name);
	irq_put_desc_busunlock(desc, flags);
}

void
irq_set_chained_handler_and_data(unsigned int irq, irq_flow_handler_t handle,
				 void *data)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_buslock(irq, &flags, 0);

	if (!desc)
		return;

	desc->irq_common_data.handler_data = data;
	__irq_do_set_handler(desc, handle, 1, NULL);

	irq_put_desc_busunlock(desc, flags);
}

void
irq_set_chip_and_handler_name(unsigned int irq, const struct irq_chip *chip,
			      irq_flow_handler_t handle, const char *name)
{
	irq_set_chip(irq, chip);
	__irq_set_handler(irq, handle, 0, name);
}

void irq_modify_status(unsigned int irq, unsigned long clr, unsigned long set)
{
	unsigned long flags, trigger, tmp;
	struct irq_desc *desc = irq_get_desc_lock(irq, &flags, 0);

	if (!desc)
		return;

	 
	WARN_ON_ONCE(!desc->depth && (set & _IRQ_NOAUTOEN));

	irq_settings_clr_and_set(desc, clr, set);

	trigger = irqd_get_trigger_type(&desc->irq_data);

	irqd_clear(&desc->irq_data, IRQD_NO_BALANCING | IRQD_PER_CPU |
		   IRQD_TRIGGER_MASK | IRQD_LEVEL | IRQD_MOVE_PCNTXT);
	if (irq_settings_has_no_balance_set(desc))
		irqd_set(&desc->irq_data, IRQD_NO_BALANCING);
	if (irq_settings_is_per_cpu(desc))
		irqd_set(&desc->irq_data, IRQD_PER_CPU);
	if (irq_settings_can_move_pcntxt(desc))
		irqd_set(&desc->irq_data, IRQD_MOVE_PCNTXT);
	if (irq_settings_is_level(desc))
		irqd_set(&desc->irq_data, IRQD_LEVEL);

	tmp = irq_settings_get_trigger_mask(desc);
	if (tmp != IRQ_TYPE_NONE)
		trigger = tmp;

	irqd_set(&desc->irq_data, trigger);

	irq_put_desc_unlock(desc, flags);
}



int irq_chip_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct irq_data *pos;

	for (pos = NULL; !pos && data; data = irqd_get_parent_data(data)) {
		if (data->chip && data->chip->irq_compose_msi_msg)
			pos = data;
	}

	if (!pos)
		return -ENOSYS;

	pos->chip->irq_compose_msi_msg(pos, msg);
	return 0;
}

static struct device *irq_get_parent_device(struct irq_data *data)
{
	if (data->domain)
		return data->domain->dev;

	return NULL;
}

int irq_chip_pm_get(struct irq_data *data)
{
	struct device *dev = irq_get_parent_device(data);
	int retval = 0;

	if (IS_ENABLED(CONFIG_PM) && dev)
		retval = pm_runtime_resume_and_get(dev);

	return retval;
}

int irq_chip_pm_put(struct irq_data *data)
{
	struct device *dev = irq_get_parent_device(data);
	int retval = 0;

	if (IS_ENABLED(CONFIG_PM) && dev)
		retval = pm_runtime_put(dev);

	return (retval < 0) ? retval : 0;
}
