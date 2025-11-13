
#define pr_fmt(fmt) "genirq: " fmt

#include <linux/irq.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/sched/task.h>
#include <linux/sched/isolation.h>
#include <uapi/linux/sched/types.h>
#include <linux/task_work.h>

#include "internals.h"

DEFINE_STATIC_KEY_FALSE(force_irqthreads_key);

static int __init setup_forced_irqthreads(char *arg)
{
	static_branch_enable(&force_irqthreads_key);
	return 0;
}
early_param("threadirqs", setup_forced_irqthreads);

static void __synchronize_hardirq(struct irq_desc *desc, bool sync_chip)
{
	struct irq_data *irqd = irq_desc_get_irq_data(desc);
	bool inprogress;

	do {
		unsigned long flags;

		while (irqd_irq_inprogress(&desc->irq_data))
			cpu_relax();

		raw_spin_lock_irqsave(&desc->lock, flags);
		inprogress = irqd_irq_inprogress(&desc->irq_data);

		if (!inprogress && sync_chip) {
			
			__irq_get_irqchip_state(irqd, IRQCHIP_STATE_ACTIVE,
						&inprogress);
		}
		raw_spin_unlock_irqrestore(&desc->lock, flags);

	} while (inprogress);
}

bool synchronize_hardirq(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (desc) {
		__synchronize_hardirq(desc, false);
		return !atomic_read(&desc->threads_active);
	}

	return true;
}

void synchronize_irq(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (desc) {
		__synchronize_hardirq(desc, true);
		
		wait_event(desc->wait_for_threads,
			   !atomic_read(&desc->threads_active));
	}
}

int irq_set_vcpu_affinity(unsigned int irq, void *vcpu_info)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_lock(irq, &flags, 0);
	struct irq_data *data;
	struct irq_chip *chip;
	int ret = -ENOSYS;

	if (!desc)
		return -EINVAL;

	data = irq_desc_get_irq_data(desc);
	do {
		chip = irq_data_get_irq_chip(data);
		if (chip && chip->irq_set_vcpu_affinity)
			break;
		data = NULL;
	} while (data);

	if (data)
		ret = chip->irq_set_vcpu_affinity(data, vcpu_info);
	irq_put_desc_unlock(desc, flags);

	return ret;
}

void __disable_irq(struct irq_desc *desc)
{
	if (!desc->depth++)
		irq_disable(desc);
}

static int __disable_irq_nosync(unsigned int irq)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_buslock(irq, &flags, IRQ_GET_DESC_CHECK_GLOBAL);

	if (!desc)
		return -EINVAL;
	__disable_irq(desc);
	irq_put_desc_busunlock(desc, flags);
	return 0;
}

void disable_irq_nosync(unsigned int irq)
{
	__disable_irq_nosync(irq);
}

void disable_irq(unsigned int irq)
{
	if (!__disable_irq_nosync(irq))
		synchronize_irq(irq);
}

bool disable_hardirq(unsigned int irq)
{
	if (!__disable_irq_nosync(irq))
		return synchronize_hardirq(irq);

	return false;
}

void disable_nmi_nosync(unsigned int irq)
{
	disable_irq_nosync(irq);
}

void __enable_irq(struct irq_desc *desc)
{
	switch (desc->depth) {
	case 0:
 err_out:
		WARN(1, KERN_WARNING "Unbalanced enable for IRQ %d\n",
		     irq_desc_get_irq(desc));
		break;
	case 1: {
		if (desc->istate & IRQS_SUSPENDED)
			goto err_out;
		
		irq_settings_set_noprobe(desc);
		
		irq_startup(desc, IRQ_RESEND, IRQ_START_FORCE);
		break;
	}
	default:
		desc->depth--;
	}
}

void enable_irq(unsigned int irq)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_buslock(irq, &flags, IRQ_GET_DESC_CHECK_GLOBAL);

	if (!desc)
		return;
	if (WARN(!desc->irq_data.chip,
		 KERN_ERR "enable_irq before setup/request_irq: irq %u\n", irq))
		goto out;

	__enable_irq(desc);
out:
	irq_put_desc_busunlock(desc, flags);
}

void enable_nmi(unsigned int irq)
{
	enable_irq(irq);
}

static int set_irq_wake_real(unsigned int irq, unsigned int on)
{
	struct irq_desc *desc = irq_to_desc(irq);
	int ret = -ENXIO;

	if (irq_desc_get_chip(desc)->flags &  IRQCHIP_SKIP_SET_WAKE)
		return 0;

	if (desc->irq_data.chip->irq_set_wake)
		ret = desc->irq_data.chip->irq_set_wake(&desc->irq_data, on);

	return ret;
}

int irq_set_irq_wake(unsigned int irq, unsigned int on)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_buslock(irq, &flags, IRQ_GET_DESC_CHECK_GLOBAL);
	int ret = 0;

	if (!desc)
		return -EINVAL;

	if (desc->istate & IRQS_NMI) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (on) {
		if (desc->wake_depth++ == 0) {
			ret = set_irq_wake_real(irq, on);
			if (ret)
				desc->wake_depth = 0;
			else
				irqd_set(&desc->irq_data, IRQD_WAKEUP_STATE);
		}
	} else {
		if (desc->wake_depth == 0) {
			WARN(1, "Unbalanced IRQ %d wake disable\n", irq);
		} else if (--desc->wake_depth == 0) {
			ret = set_irq_wake_real(irq, on);
			if (ret)
				desc->wake_depth = 1;
			else
				irqd_clear(&desc->irq_data, IRQD_WAKEUP_STATE);
		}
	}

out_unlock:
	irq_put_desc_busunlock(desc, flags);
	return ret;
}

int can_request_irq(unsigned int irq, unsigned long irqflags)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_lock(irq, &flags, 0);
	int canrequest = 0;

	if (!desc)
		return 0;

	if (irq_settings_can_request(desc)) {
		if (!desc->action ||
		    irqflags & desc->action->flags & IRQF_SHARED)
			canrequest = 1;
	}
	irq_put_desc_unlock(desc, flags);
	return canrequest;
}

int __irq_set_trigger(struct irq_desc *desc, unsigned long flags)
{
	struct irq_chip *chip = desc->irq_data.chip;
	int ret, unmask = 0;

	if (!chip || !chip->irq_set_type) {
		
		pr_debug("No set_type function for IRQ %d (%s)\n",
			 irq_desc_get_irq(desc),
			 chip ? (chip->name ? : "unknown") : "unknown");
		return 0;
	}

	if (chip->flags & IRQCHIP_SET_TYPE_MASKED) {
		if (!irqd_irq_masked(&desc->irq_data))
			mask_irq(desc);
		if (!irqd_irq_disabled(&desc->irq_data))
			unmask = 1;
	}

	flags &= IRQ_TYPE_SENSE_MASK;
	ret = chip->irq_set_type(&desc->irq_data, flags);

	switch (ret) {
	case IRQ_SET_MASK_OK:
	case IRQ_SET_MASK_OK_DONE:
		irqd_clear(&desc->irq_data, IRQD_TRIGGER_MASK);
		irqd_set(&desc->irq_data, flags);
		fallthrough;

	case IRQ_SET_MASK_OK_NOCOPY:
		flags = irqd_get_trigger_type(&desc->irq_data);
		irq_settings_set_trigger_mask(desc, flags);
		irqd_clear(&desc->irq_data, IRQD_LEVEL);
		irq_settings_clr_level(desc);
		if (flags & IRQ_TYPE_LEVEL_MASK) {
			irq_settings_set_level(desc);
			irqd_set(&desc->irq_data, IRQD_LEVEL);
		}

		ret = 0;
		break;
	default:
		pr_err("Setting trigger mode %lu for irq %u failed (%pS)\n",
		       flags, irq_desc_get_irq(desc), chip->irq_set_type);
	}
	if (unmask)
		unmask_irq(desc);
	return ret;
}

int irq_set_parent(int irq, int parent_irq)
{
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_lock(irq, &flags, 0);

	if (!desc)
		return -EINVAL;

	desc->parent_irq = parent_irq;

	irq_put_desc_unlock(desc, flags);
	return 0;
}

static irqreturn_t irq_default_primary_handler(int irq, void *dev_id)
{
	return IRQ_WAKE_THREAD;
}

static irqreturn_t irq_nested_primary_handler(int irq, void *dev_id)
{
	WARN(1, "Primary handler called for nested irq %d\n", irq);
	return IRQ_NONE;
}

static irqreturn_t irq_forced_secondary_handler(int irq, void *dev_id)
{
	WARN(1, "Secondary action handler called for irq %d\n", irq);
	return IRQ_NONE;
}

static int irq_wait_for_interrupt(struct irqaction *action)
{
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (kthread_should_stop()) {
			
			if (test_and_clear_bit(IRQTF_RUNTHREAD,
					       &action->thread_flags)) {
				__set_current_state(TASK_RUNNING);
				return 0;
			}
			__set_current_state(TASK_RUNNING);
			return -1;
		}

		if (test_and_clear_bit(IRQTF_RUNTHREAD,
				       &action->thread_flags)) {
			__set_current_state(TASK_RUNNING);
			return 0;
		}
		schedule();
	}
}

static void irq_finalize_oneshot(struct irq_desc *desc,
				 struct irqaction *action)
{
	if (!(desc->istate & IRQS_ONESHOT) ||
	    action->handler == irq_forced_secondary_handler)
		return;
again:
	chip_bus_lock(desc);
	raw_spin_lock_irq(&desc->lock);

	if (unlikely(irqd_irq_inprogress(&desc->irq_data))) {
		raw_spin_unlock_irq(&desc->lock);
		chip_bus_sync_unlock(desc);
		cpu_relax();
		goto again;
	}

	if (test_bit(IRQTF_RUNTHREAD, &action->thread_flags))
		goto out_unlock;

	desc->threads_oneshot &= ~action->thread_mask;

	if (!desc->threads_oneshot && !irqd_irq_disabled(&desc->irq_data) &&
	    irqd_irq_masked(&desc->irq_data))
		unmask_threaded_irq(desc);

out_unlock:
	raw_spin_unlock_irq(&desc->lock);
	chip_bus_sync_unlock(desc);
}

static inline void
irq_thread_check_affinity(struct irq_desc *desc, struct irqaction *action) { }

static irqreturn_t
irq_forced_thread_fn(struct irq_desc *desc, struct irqaction *action)
{
	irqreturn_t ret;

	local_bh_disable();
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		local_irq_disable();
	ret = action->thread_fn(action->irq, action->dev_id);
	if (ret == IRQ_HANDLED)
		atomic_inc(&desc->threads_handled);

	irq_finalize_oneshot(desc, action);
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		local_irq_enable();
	local_bh_enable();
	return ret;
}

static irqreturn_t irq_thread_fn(struct irq_desc *desc,
		struct irqaction *action)
{
	irqreturn_t ret;

	ret = action->thread_fn(action->irq, action->dev_id);
	if (ret == IRQ_HANDLED)
		atomic_inc(&desc->threads_handled);

	irq_finalize_oneshot(desc, action);
	return ret;
}

static void wake_threads_waitq(struct irq_desc *desc)
{
	if (atomic_dec_and_test(&desc->threads_active))
		wake_up(&desc->wait_for_threads);
}

static void irq_thread_dtor(struct callback_head *unused)
{
	struct task_struct *tsk = current;
	struct irq_desc *desc;
	struct irqaction *action;

	if (WARN_ON_ONCE(!(current->flags & PF_EXITING)))
		return;

	action = kthread_data(tsk);

	pr_err("exiting task \"%s\" (%d) is an active IRQ thread (irq %d)\n",
	       tsk->comm, tsk->pid, action->irq);

	desc = irq_to_desc(action->irq);
	
	if (test_and_clear_bit(IRQTF_RUNTHREAD, &action->thread_flags))
		wake_threads_waitq(desc);

	irq_finalize_oneshot(desc, action);
}

static void irq_wake_secondary(struct irq_desc *desc, struct irqaction *action)
{
	struct irqaction *secondary = action->secondary;

	if (WARN_ON_ONCE(!secondary))
		return;

	raw_spin_lock_irq(&desc->lock);
	__irq_wake_thread(desc, secondary);
	raw_spin_unlock_irq(&desc->lock);
}

static void irq_thread_set_ready(struct irq_desc *desc,
				 struct irqaction *action)
{
	set_bit(IRQTF_READY, &action->thread_flags);
	wake_up(&desc->wait_for_threads);
}

static void wake_up_and_wait_for_irq_thread_ready(struct irq_desc *desc,
						  struct irqaction *action)
{
	if (!action || !action->thread)
		return;

	wake_up_process(action->thread);
	wait_event(desc->wait_for_threads,
		   test_bit(IRQTF_READY, &action->thread_flags));
}

static int irq_thread(void *data)
{
	struct callback_head on_exit_work;
	struct irqaction *action = data;
	struct irq_desc *desc = irq_to_desc(action->irq);
	irqreturn_t (*handler_fn)(struct irq_desc *desc,
			struct irqaction *action);

	irq_thread_set_ready(desc, action);

	sched_set_fifo(current);

	if (force_irqthreads() && test_bit(IRQTF_FORCED_THREAD,
					   &action->thread_flags))
		handler_fn = irq_forced_thread_fn;
	else
		handler_fn = irq_thread_fn;

	init_task_work(&on_exit_work, irq_thread_dtor);
	task_work_add(current, &on_exit_work, TWA_NONE);

	irq_thread_check_affinity(desc, action);

	while (!irq_wait_for_interrupt(action)) {
		irqreturn_t action_ret;

		irq_thread_check_affinity(desc, action);

		action_ret = handler_fn(desc, action);
		if (action_ret == IRQ_WAKE_THREAD)
			irq_wake_secondary(desc, action);

		wake_threads_waitq(desc);
	}

	task_work_cancel(current, irq_thread_dtor);
	return 0;
}

void irq_wake_thread(unsigned int irq, void *dev_id)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irqaction *action;
	unsigned long flags;

	if (!desc || WARN_ON(irq_settings_is_per_cpu_devid(desc)))
		return;

	raw_spin_lock_irqsave(&desc->lock, flags);
	for_each_action_of_desc(desc, action) {
		if (action->dev_id == dev_id) {
			if (action->thread)
				__irq_wake_thread(desc, action);
			break;
		}
	}
	raw_spin_unlock_irqrestore(&desc->lock, flags);
}

static int irq_setup_forced_threading(struct irqaction *new)
{
	if (!force_irqthreads())
		return 0;
	if (new->flags & (IRQF_NO_THREAD | IRQF_PERCPU | IRQF_ONESHOT))
		return 0;

	if (new->handler == irq_default_primary_handler)
		return 0;

	new->flags |= IRQF_ONESHOT;

	if (new->handler && new->thread_fn) {
		
		new->secondary = kzalloc(sizeof(struct irqaction), GFP_KERNEL);
		if (!new->secondary)
			return -ENOMEM;
		new->secondary->handler = irq_forced_secondary_handler;
		new->secondary->thread_fn = new->thread_fn;
		new->secondary->dev_id = new->dev_id;
		new->secondary->irq = new->irq;
		new->secondary->name = new->name;
	}
	
	set_bit(IRQTF_FORCED_THREAD, &new->thread_flags);
	new->thread_fn = new->handler;
	new->handler = irq_default_primary_handler;
	return 0;
}

static int irq_request_resources(struct irq_desc *desc)
{
	struct irq_data *d = &desc->irq_data;
	struct irq_chip *c = d->chip;

	return c->irq_request_resources ? c->irq_request_resources(d) : 0;
}

static void irq_release_resources(struct irq_desc *desc)
{
	struct irq_data *d = &desc->irq_data;
	struct irq_chip *c = d->chip;

	if (c->irq_release_resources)
		c->irq_release_resources(d);
}

static bool irq_supports_nmi(struct irq_desc *desc)
{
	struct irq_data *d = irq_desc_get_irq_data(desc);

	if (d->chip->irq_bus_lock || d->chip->irq_bus_sync_unlock)
		return false;

	return d->chip->flags & IRQCHIP_SUPPORTS_NMI;
}

static int irq_nmi_setup(struct irq_desc *desc)
{
	struct irq_data *d = irq_desc_get_irq_data(desc);
	struct irq_chip *c = d->chip;

	return c->irq_nmi_setup ? c->irq_nmi_setup(d) : -EINVAL;
}

static void irq_nmi_teardown(struct irq_desc *desc)
{
	struct irq_data *d = irq_desc_get_irq_data(desc);
	struct irq_chip *c = d->chip;

	if (c->irq_nmi_teardown)
		c->irq_nmi_teardown(d);
}

static int
setup_irq_thread(struct irqaction *new, unsigned int irq, bool secondary)
{
	struct task_struct *t;

	if (!secondary) {
		t = kthread_create(irq_thread, new, "irq/%d-%s", irq,
				   new->name);
	} else {
		t = kthread_create(irq_thread, new, "irq/%d-s-%s", irq,
				   new->name);
	}

	if (IS_ERR(t))
		return PTR_ERR(t);

	new->thread = get_task_struct(t);
	
	set_bit(IRQTF_AFFINITY, &new->thread_flags);
	return 0;
}

static int
__setup_irq(unsigned int irq, struct irq_desc *desc, struct irqaction *new)
{
	struct irqaction *old, **old_ptr;
	unsigned long flags, thread_mask = 0;
	int ret, nested, shared = 0;

	if (!desc)
		return -EINVAL;

	if (desc->irq_data.chip == &no_irq_chip)
		return -ENOSYS;
	if (!try_module_get(desc->owner))
		return -ENODEV;

	new->irq = irq;

	if (!(new->flags & IRQF_TRIGGER_MASK))
		new->flags |= irqd_get_trigger_type(&desc->irq_data);

	nested = irq_settings_is_nested_thread(desc);
	if (nested) {
		if (!new->thread_fn) {
			ret = -EINVAL;
			goto out_mput;
		}
		
		new->handler = irq_nested_primary_handler;
	} else {
		if (irq_settings_can_thread(desc)) {
			ret = irq_setup_forced_threading(new);
			if (ret)
				goto out_mput;
		}
	}

	if (new->thread_fn && !nested) {
		ret = setup_irq_thread(new, irq, false);
		if (ret)
			goto out_mput;
		if (new->secondary) {
			ret = setup_irq_thread(new->secondary, irq, true);
			if (ret)
				goto out_thread;
		}
	}

	if (desc->irq_data.chip->flags & IRQCHIP_ONESHOT_SAFE)
		new->flags &= ~IRQF_ONESHOT;

	mutex_lock(&desc->request_mutex);

	chip_bus_lock(desc);

	if (!desc->action) {
		ret = irq_request_resources(desc);
		if (ret) {
			pr_err("Failed to request resources for %s (irq %d) on irqchip %s\n",
			       new->name, irq, desc->irq_data.chip->name);
			goto out_bus_unlock;
		}
	}

	raw_spin_lock_irqsave(&desc->lock, flags);
	old_ptr = &desc->action;
	old = *old_ptr;
	if (old) {
		
		unsigned int oldtype;

		if (desc->istate & IRQS_NMI) {
			pr_err("Invalid attempt to share NMI for %s (irq %d) on irqchip %s.\n",
				new->name, irq, desc->irq_data.chip->name);
			ret = -EINVAL;
			goto out_unlock;
		}

		if (irqd_trigger_type_was_set(&desc->irq_data)) {
			oldtype = irqd_get_trigger_type(&desc->irq_data);
		} else {
			oldtype = new->flags & IRQF_TRIGGER_MASK;
			irqd_set_trigger_type(&desc->irq_data, oldtype);
		}

		if (!((old->flags & new->flags) & IRQF_SHARED) ||
		    (oldtype != (new->flags & IRQF_TRIGGER_MASK)) ||
		    ((old->flags ^ new->flags) & IRQF_ONESHOT))
			goto mismatch;

		if ((old->flags & IRQF_PERCPU) !=
		    (new->flags & IRQF_PERCPU))
			goto mismatch;

		do {
			
			thread_mask |= old->thread_mask;
			old_ptr = &old->next;
			old = *old_ptr;
		} while (old);
		shared = 1;
	}

	if (new->flags & IRQF_ONESHOT) {
		
		if (thread_mask == ~0UL) {
			ret = -EBUSY;
			goto out_unlock;
		}
		
		new->thread_mask = 1UL << ffz(thread_mask);

	} else if (new->handler == irq_default_primary_handler &&
		   !(desc->irq_data.chip->flags & IRQCHIP_ONESHOT_SAFE)) {
		
		pr_err("Threaded irq requested with handler=NULL and !ONESHOT for %s (irq %d)\n",
		       new->name, irq);
		ret = -EINVAL;
		goto out_unlock;
	}

	if (!shared) {
		
		if (new->flags & IRQF_TRIGGER_MASK) {
			ret = __irq_set_trigger(desc,
						new->flags & IRQF_TRIGGER_MASK);

			if (ret)
				goto out_unlock;
		}

		ret = irq_activate(desc);
		if (ret)
			goto out_unlock;

		desc->istate &= ~(IRQS_AUTODETECT | IRQS_SPURIOUS_DISABLED | \
				  IRQS_ONESHOT | IRQS_WAITING);
		irqd_clear(&desc->irq_data, IRQD_IRQ_INPROGRESS);

		if (new->flags & IRQF_PERCPU) {
			irqd_set(&desc->irq_data, IRQD_PER_CPU);
			irq_settings_set_per_cpu(desc);
			if (new->flags & IRQF_NO_DEBUG)
				irq_settings_set_no_debug(desc);
		}

		if (noirqdebug)
			irq_settings_set_no_debug(desc);

		if (new->flags & IRQF_ONESHOT)
			desc->istate |= IRQS_ONESHOT;

		if (new->flags & IRQF_NOBALANCING) {
			irq_settings_set_no_balancing(desc);
			irqd_set(&desc->irq_data, IRQD_NO_BALANCING);
		}

		if (!(new->flags & IRQF_NO_AUTOEN) &&
		    irq_settings_can_autoenable(desc)) {
			irq_startup(desc, IRQ_RESEND, IRQ_START_COND);
		} else {
			
			WARN_ON_ONCE(new->flags & IRQF_SHARED);
			
			desc->depth = 1;
		}

	} else if (new->flags & IRQF_TRIGGER_MASK) {
		unsigned int nmsk = new->flags & IRQF_TRIGGER_MASK;
		unsigned int omsk = irqd_get_trigger_type(&desc->irq_data);

		if (nmsk != omsk)
			
			pr_warn("irq %d uses trigger mode %u; requested %u\n",
				irq, omsk, nmsk);
	}

	*old_ptr = new;

	irq_pm_install_action(desc, new);

	desc->irq_count = 0;
	desc->irqs_unhandled = 0;

	if (shared && (desc->istate & IRQS_SPURIOUS_DISABLED)) {
		desc->istate &= ~IRQS_SPURIOUS_DISABLED;
		__enable_irq(desc);
	}

	raw_spin_unlock_irqrestore(&desc->lock, flags);
	chip_bus_sync_unlock(desc);
	mutex_unlock(&desc->request_mutex);

	irq_setup_timings(desc, new);

	wake_up_and_wait_for_irq_thread_ready(desc, new);
	wake_up_and_wait_for_irq_thread_ready(desc, new->secondary);

	register_irq_proc(irq, desc);
	new->dir = NULL;
	register_handler_proc(irq, new);
	return 0;

mismatch:
	if (!(new->flags & IRQF_PROBE_SHARED)) {
		pr_err("Flags mismatch irq %d. %08x (%s) vs. %08x (%s)\n",
		       irq, new->flags, new->name, old->flags, old->name);
	}
	ret = -EBUSY;

out_unlock:
	raw_spin_unlock_irqrestore(&desc->lock, flags);

	if (!desc->action)
		irq_release_resources(desc);
out_bus_unlock:
	chip_bus_sync_unlock(desc);
	mutex_unlock(&desc->request_mutex);

out_thread:
	if (new->thread) {
		struct task_struct *t = new->thread;

		new->thread = NULL;
		kthread_stop(t);
		put_task_struct(t);
	}
	if (new->secondary && new->secondary->thread) {
		struct task_struct *t = new->secondary->thread;

		new->secondary->thread = NULL;
		kthread_stop(t);
		put_task_struct(t);
	}
out_mput:
	module_put(desc->owner);
	return ret;
}

static struct irqaction *__free_irq(struct irq_desc *desc, void *dev_id)
{
	unsigned irq = desc->irq_data.irq;
	struct irqaction *action, **action_ptr;
	unsigned long flags;

	WARN(in_interrupt(), "Trying to free IRQ %d from IRQ context!\n", irq);

	mutex_lock(&desc->request_mutex);
	chip_bus_lock(desc);
	raw_spin_lock_irqsave(&desc->lock, flags);

	action_ptr = &desc->action;
	for (;;) {
		action = *action_ptr;

		if (!action) {
			WARN(1, "Trying to free already-free IRQ %d\n", irq);
			raw_spin_unlock_irqrestore(&desc->lock, flags);
			chip_bus_sync_unlock(desc);
			mutex_unlock(&desc->request_mutex);
			return NULL;
		}

		if (action->dev_id == dev_id)
			break;
		action_ptr = &action->next;
	}

	*action_ptr = action->next;

	irq_pm_remove_action(desc, action);

	if (!desc->action) {
		irq_settings_clr_disable_unlazy(desc);
		
		irq_shutdown(desc);
	}

	raw_spin_unlock_irqrestore(&desc->lock, flags);
	
	chip_bus_sync_unlock(desc);

	unregister_handler_proc(irq, action);

	__synchronize_hardirq(desc, true);

	if (action->thread) {
		kthread_stop(action->thread);
		put_task_struct(action->thread);
		if (action->secondary && action->secondary->thread) {
			kthread_stop(action->secondary->thread);
			put_task_struct(action->secondary->thread);
		}
	}

	if (!desc->action) {
		
		chip_bus_lock(desc);
		
		raw_spin_lock_irqsave(&desc->lock, flags);
		irq_domain_deactivate_irq(&desc->irq_data);
		raw_spin_unlock_irqrestore(&desc->lock, flags);

		irq_release_resources(desc);
		chip_bus_sync_unlock(desc);
		irq_remove_timings(desc);
	}

	mutex_unlock(&desc->request_mutex);

	irq_chip_pm_put(&desc->irq_data);
	module_put(desc->owner);
	kfree(action->secondary);
	return action;
}

const void *free_irq(unsigned int irq, void *dev_id)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irqaction *action;
	const char *devname;

	if (!desc || WARN_ON(irq_settings_is_per_cpu_devid(desc)))
		return NULL;

	action = __free_irq(desc, dev_id);

	if (!action)
		return NULL;

	devname = action->name;
	kfree(action);
	return devname;
}

static const void *__cleanup_nmi(unsigned int irq, struct irq_desc *desc)
{
	const char *devname = NULL;

	desc->istate &= ~IRQS_NMI;

	if (!WARN_ON(desc->action == NULL)) {
		irq_pm_remove_action(desc, desc->action);
		devname = desc->action->name;
		unregister_handler_proc(irq, desc->action);

		kfree(desc->action);
		desc->action = NULL;
	}

	irq_settings_clr_disable_unlazy(desc);
	irq_shutdown_and_deactivate(desc);

	irq_release_resources(desc);

	irq_chip_pm_put(&desc->irq_data);
	module_put(desc->owner);

	return devname;
}

const void *free_nmi(unsigned int irq, void *dev_id)
{
	struct irq_desc *desc = irq_to_desc(irq);
	unsigned long flags;
	const void *devname;

	if (!desc || WARN_ON(!(desc->istate & IRQS_NMI)))
		return NULL;

	if (WARN_ON(irq_settings_is_per_cpu_devid(desc)))
		return NULL;

	if (WARN_ON(desc->depth == 0))
		disable_nmi_nosync(irq);

	raw_spin_lock_irqsave(&desc->lock, flags);

	irq_nmi_teardown(desc);
	devname = __cleanup_nmi(irq, desc);

	raw_spin_unlock_irqrestore(&desc->lock, flags);

	return devname;
}

int request_threaded_irq(unsigned int irq, irq_handler_t handler,
			 irq_handler_t thread_fn, unsigned long irqflags,
			 const char *devname, void *dev_id)
{
	struct irqaction *action;
	struct irq_desc *desc;
	int retval;

	if (irq == IRQ_NOTCONNECTED)
		return -ENOTCONN;

	if (((irqflags & IRQF_SHARED) && !dev_id) ||
	    ((irqflags & IRQF_SHARED) && (irqflags & IRQF_NO_AUTOEN)) ||
	    (!(irqflags & IRQF_SHARED) && (irqflags & IRQF_COND_SUSPEND)) ||
	    ((irqflags & IRQF_NO_SUSPEND) && (irqflags & IRQF_COND_SUSPEND)))
		return -EINVAL;

	desc = irq_to_desc(irq);
	if (!desc)
		return -EINVAL;

	if (!irq_settings_can_request(desc) ||
	    WARN_ON(irq_settings_is_per_cpu_devid(desc)))
		return -EINVAL;

	if (!handler) {
		if (!thread_fn)
			return -EINVAL;
		handler = irq_default_primary_handler;
	}

	action = kzalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->thread_fn = thread_fn;
	action->flags = irqflags;
	action->name = devname;
	action->dev_id = dev_id;

	retval = irq_chip_pm_get(&desc->irq_data);
	if (retval < 0) {
		kfree(action);
		return retval;
	}

	retval = __setup_irq(irq, desc, action);

	if (retval) {
		irq_chip_pm_put(&desc->irq_data);
		kfree(action->secondary);
		kfree(action);
	}

	return retval;
}

int request_any_context_irq(unsigned int irq, irq_handler_t handler,
			    unsigned long flags, const char *name, void *dev_id)
{
	struct irq_desc *desc;
	int ret;

	if (irq == IRQ_NOTCONNECTED)
		return -ENOTCONN;

	desc = irq_to_desc(irq);
	if (!desc)
		return -EINVAL;

	if (irq_settings_is_nested_thread(desc)) {
		ret = request_threaded_irq(irq, NULL, handler,
					   flags, name, dev_id);
		return !ret ? IRQC_IS_NESTED : ret;
	}

	ret = request_irq(irq, handler, flags, name, dev_id);
	return !ret ? IRQC_IS_HARDIRQ : ret;
}

int request_nmi(unsigned int irq, irq_handler_t handler,
		unsigned long irqflags, const char *name, void *dev_id)
{
	struct irqaction *action;
	struct irq_desc *desc;
	unsigned long flags;
	int retval;

	if (irq == IRQ_NOTCONNECTED)
		return -ENOTCONN;

	if (irqflags & (IRQF_SHARED | IRQF_COND_SUSPEND | IRQF_IRQPOLL))
		return -EINVAL;

	if (!(irqflags & IRQF_PERCPU))
		return -EINVAL;

	if (!handler)
		return -EINVAL;

	desc = irq_to_desc(irq);

	if (!desc || (irq_settings_can_autoenable(desc) &&
	    !(irqflags & IRQF_NO_AUTOEN)) ||
	    !irq_settings_can_request(desc) ||
	    WARN_ON(irq_settings_is_per_cpu_devid(desc)) ||
	    !irq_supports_nmi(desc))
		return -EINVAL;

	action = kzalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags | IRQF_NO_THREAD | IRQF_NOBALANCING;
	action->name = name;
	action->dev_id = dev_id;

	retval = irq_chip_pm_get(&desc->irq_data);
	if (retval < 0)
		goto err_out;

	retval = __setup_irq(irq, desc, action);
	if (retval)
		goto err_irq_setup;

	raw_spin_lock_irqsave(&desc->lock, flags);

	desc->istate |= IRQS_NMI;
	retval = irq_nmi_setup(desc);
	if (retval) {
		__cleanup_nmi(irq, desc);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
		return -EINVAL;
	}

	raw_spin_unlock_irqrestore(&desc->lock, flags);

	return 0;

err_irq_setup:
	irq_chip_pm_put(&desc->irq_data);
err_out:
	kfree(action);

	return retval;
}

void enable_percpu_irq(unsigned int irq, unsigned int type)
{
	unsigned int cpu = smp_processor_id();
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_lock(irq, &flags, IRQ_GET_DESC_CHECK_PERCPU);

	if (!desc)
		return;

	type &= IRQ_TYPE_SENSE_MASK;
	if (type == IRQ_TYPE_NONE)
		type = irqd_get_trigger_type(&desc->irq_data);

	if (type != IRQ_TYPE_NONE) {
		int ret;

		ret = __irq_set_trigger(desc, type);

		if (ret) {
			WARN(1, "failed to set type for IRQ%d\n", irq);
			goto out;
		}
	}

	irq_percpu_enable(desc, cpu);
out:
	irq_put_desc_unlock(desc, flags);
}

void enable_percpu_nmi(unsigned int irq, unsigned int type)
{
	enable_percpu_irq(irq, type);
}

bool irq_percpu_is_enabled(unsigned int irq)
{
	unsigned int cpu = smp_processor_id();
	struct irq_desc *desc;
	unsigned long flags;
	bool is_enabled;

	desc = irq_get_desc_lock(irq, &flags, IRQ_GET_DESC_CHECK_PERCPU);
	if (!desc)
		return false;

	is_enabled = cpumask_test_cpu(cpu, desc->percpu_enabled);
	irq_put_desc_unlock(desc, flags);

	return is_enabled;
}

void disable_percpu_irq(unsigned int irq)
{
	unsigned int cpu = smp_processor_id();
	unsigned long flags;
	struct irq_desc *desc = irq_get_desc_lock(irq, &flags, IRQ_GET_DESC_CHECK_PERCPU);

	if (!desc)
		return;

	irq_percpu_disable(desc, cpu);
	irq_put_desc_unlock(desc, flags);
}

void disable_percpu_nmi(unsigned int irq)
{
	disable_percpu_irq(irq);
}

static struct irqaction *__free_percpu_irq(unsigned int irq, void __percpu *dev_id)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irqaction *action;
	unsigned long flags;

	WARN(in_interrupt(), "Trying to free IRQ %d from IRQ context!\n", irq);

	if (!desc)
		return NULL;

	raw_spin_lock_irqsave(&desc->lock, flags);

	action = desc->action;
	if (!action || action->percpu_dev_id != dev_id) {
		WARN(1, "Trying to free already-free IRQ %d\n", irq);
		goto bad;
	}

	if (!cpumask_empty(desc->percpu_enabled)) {
		WARN(1, "percpu IRQ %d still enabled on CPU%d!\n",
		     irq, cpumask_first(desc->percpu_enabled));
		goto bad;
	}

	desc->action = NULL;

	desc->istate &= ~IRQS_NMI;

	raw_spin_unlock_irqrestore(&desc->lock, flags);

	unregister_handler_proc(irq, action);

	irq_chip_pm_put(&desc->irq_data);
	module_put(desc->owner);
	return action;

bad:
	raw_spin_unlock_irqrestore(&desc->lock, flags);
	return NULL;
}

void remove_percpu_irq(unsigned int irq, struct irqaction *act)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (desc && irq_settings_is_per_cpu_devid(desc))
	    __free_percpu_irq(irq, act->percpu_dev_id);
}

void free_percpu_irq(unsigned int irq, void __percpu *dev_id)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (!desc || !irq_settings_is_per_cpu_devid(desc))
		return;

	chip_bus_lock(desc);
	kfree(__free_percpu_irq(irq, dev_id));
	chip_bus_sync_unlock(desc);
}

void free_percpu_nmi(unsigned int irq, void __percpu *dev_id)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (!desc || !irq_settings_is_per_cpu_devid(desc))
		return;

	if (WARN_ON(!(desc->istate & IRQS_NMI)))
		return;

	kfree(__free_percpu_irq(irq, dev_id));
}

int setup_percpu_irq(unsigned int irq, struct irqaction *act)
{
	struct irq_desc *desc = irq_to_desc(irq);
	int retval;

	if (!desc || !irq_settings_is_per_cpu_devid(desc))
		return -EINVAL;

	retval = irq_chip_pm_get(&desc->irq_data);
	if (retval < 0)
		return retval;

	retval = __setup_irq(irq, desc, act);

	if (retval)
		irq_chip_pm_put(&desc->irq_data);

	return retval;
}

int __request_percpu_irq(unsigned int irq, irq_handler_t handler,
			 unsigned long flags, const char *devname,
			 void __percpu *dev_id)
{
	struct irqaction *action;
	struct irq_desc *desc;
	int retval;

	if (!dev_id)
		return -EINVAL;

	desc = irq_to_desc(irq);
	if (!desc || !irq_settings_can_request(desc) ||
	    !irq_settings_is_per_cpu_devid(desc))
		return -EINVAL;

	if (flags && flags != IRQF_TIMER)
		return -EINVAL;

	action = kzalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = flags | IRQF_PERCPU | IRQF_NO_SUSPEND;
	action->name = devname;
	action->percpu_dev_id = dev_id;

	retval = irq_chip_pm_get(&desc->irq_data);
	if (retval < 0) {
		kfree(action);
		return retval;
	}

	retval = __setup_irq(irq, desc, action);

	if (retval) {
		irq_chip_pm_put(&desc->irq_data);
		kfree(action);
	}

	return retval;
}

int request_percpu_nmi(unsigned int irq, irq_handler_t handler,
		       const char *name, void __percpu *dev_id)
{
	struct irqaction *action;
	struct irq_desc *desc;
	unsigned long flags;
	int retval;

	if (!handler)
		return -EINVAL;

	desc = irq_to_desc(irq);

	if (!desc || !irq_settings_can_request(desc) ||
	    !irq_settings_is_per_cpu_devid(desc) ||
	    irq_settings_can_autoenable(desc) ||
	    !irq_supports_nmi(desc))
		return -EINVAL;

	if (desc->istate & IRQS_NMI)
		return -EINVAL;

	action = kzalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = IRQF_PERCPU | IRQF_NO_SUSPEND | IRQF_NO_THREAD
		| IRQF_NOBALANCING;
	action->name = name;
	action->percpu_dev_id = dev_id;

	retval = irq_chip_pm_get(&desc->irq_data);
	if (retval < 0)
		goto err_out;

	retval = __setup_irq(irq, desc, action);
	if (retval)
		goto err_irq_setup;

	raw_spin_lock_irqsave(&desc->lock, flags);
	desc->istate |= IRQS_NMI;
	raw_spin_unlock_irqrestore(&desc->lock, flags);

	return 0;

err_irq_setup:
	irq_chip_pm_put(&desc->irq_data);
err_out:
	kfree(action);

	return retval;
}

int prepare_percpu_nmi(unsigned int irq)
{
	unsigned long flags;
	struct irq_desc *desc;
	int ret = 0;

	WARN_ON(preemptible());

	desc = irq_get_desc_lock(irq, &flags,
				 IRQ_GET_DESC_CHECK_PERCPU);
	if (!desc)
		return -EINVAL;

	if (WARN(!(desc->istate & IRQS_NMI),
		 KERN_ERR "prepare_percpu_nmi called for a non-NMI interrupt: irq %u\n",
		 irq)) {
		ret = -EINVAL;
		goto out;
	}

	ret = irq_nmi_setup(desc);
	if (ret) {
		pr_err("Failed to setup NMI delivery: irq %u\n", irq);
		goto out;
	}

out:
	irq_put_desc_unlock(desc, flags);
	return ret;
}

void teardown_percpu_nmi(unsigned int irq)
{
	unsigned long flags;
	struct irq_desc *desc;

	WARN_ON(preemptible());

	desc = irq_get_desc_lock(irq, &flags,
				 IRQ_GET_DESC_CHECK_PERCPU);
	if (!desc)
		return;

	if (WARN_ON(!(desc->istate & IRQS_NMI)))
		goto out;

	irq_nmi_teardown(desc);
out:
	irq_put_desc_unlock(desc, flags);
}

int __irq_get_irqchip_state(struct irq_data *data, enum irqchip_irq_state which,
			    bool *state)
{
	struct irq_chip *chip;
	int err = -EINVAL;

	do {
		chip = irq_data_get_irq_chip(data);
		if (WARN_ON_ONCE(!chip))
			return -ENODEV;
		if (chip->irq_get_irqchip_state)
			break;
		data = NULL;
	} while (data);

	if (data)
		err = chip->irq_get_irqchip_state(data, which, state);
	return err;
}

int irq_get_irqchip_state(unsigned int irq, enum irqchip_irq_state which,
			  bool *state)
{
	struct irq_desc *desc;
	struct irq_data *data;
	unsigned long flags;
	int err = -EINVAL;

	desc = irq_get_desc_buslock(irq, &flags, 0);
	if (!desc)
		return err;

	data = irq_desc_get_irq_data(desc);

	err = __irq_get_irqchip_state(data, which, state);

	irq_put_desc_busunlock(desc, flags);
	return err;
}

int irq_set_irqchip_state(unsigned int irq, enum irqchip_irq_state which,
			  bool val)
{
	struct irq_desc *desc;
	struct irq_data *data;
	struct irq_chip *chip;
	unsigned long flags;
	int err = -EINVAL;

	desc = irq_get_desc_buslock(irq, &flags, 0);
	if (!desc)
		return err;

	data = irq_desc_get_irq_data(desc);

	do {
		chip = irq_data_get_irq_chip(data);
		if (WARN_ON_ONCE(!chip)) {
			err = -ENODEV;
			goto out_unlock;
		}
		if (chip->irq_set_irqchip_state)
			break;
		data = NULL;
	} while (data);

	if (data)
		err = chip->irq_set_irqchip_state(data, which, val);

out_unlock:
	irq_put_desc_busunlock(desc, flags);
	return err;
}

bool irq_has_action(unsigned int irq)
{
	bool res;

	rcu_read_lock();
	res = irq_desc_has_action(irq_to_desc(irq));
	rcu_read_unlock();
	return res;
}

bool irq_check_status_bit(unsigned int irq, unsigned int bitmask)
{
	struct irq_desc *desc;
	bool res = false;

	rcu_read_lock();
	desc = irq_to_desc(irq);
	if (desc)
		res = !!(desc->status_use_accessors & bitmask);
	rcu_read_unlock();
	return res;
}
