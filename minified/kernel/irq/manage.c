
#define pr_fmt(fmt) "genirq: " fmt

#include <linux/irq.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/sched/task.h>
#include <linux/sched/isolation.h>
#include <linux/sched/types.h>
#include <linux/task_work.h>

#include "internals.h"

/* noirqdebug removed - always false, no way to set it */

/* force_irqthreads_key removed - macro never called */

/* __synchronize_hardirq removed - only caller was __free_irq which was removed */

/* __disable_irq, __disable_irq_nosync, disable_irq_nosync removed - never called */

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

/* enable_irq removed - never called */

int __irq_set_trigger(struct irq_desc *desc, unsigned long flags)
{
	struct irq_chip *chip = desc->irq_data.chip;
	int ret, unmask = 0;

	if (!chip || !chip->irq_set_type) {
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
		/* irq_settings_set_trigger_mask inlined - single caller */
		desc->status_use_accessors &= ~IRQ_TYPE_SENSE_MASK;
		desc->status_use_accessors |= flags & IRQ_TYPE_SENSE_MASK;
		irqd_clear(&desc->irq_data, IRQD_LEVEL);
		desc->status_use_accessors &=
			~_IRQ_LEVEL; /* irq_settings_clr_level inlined */
		if (flags & IRQ_TYPE_LEVEL_MASK) {
			desc->status_use_accessors |=
				_IRQ_LEVEL; /* irq_settings_set_level inlined */
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

/* irq_wait_for_interrupt inlined into irq_thread */

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

/* irq_thread_check_affinity removed - empty function, CONFIG_SMP not enabled */
/* irq_forced_thread_fn removed - never called (~16 LOC) */

static irqreturn_t irq_thread_fn(struct irq_desc *desc,
				 struct irqaction *action)
{
	irqreturn_t ret;

	ret = action->thread_fn(action->irq, action->dev_id);
	if (ret == IRQ_HANDLED)
		/* threads_handled inc removed - field removed */

		irq_finalize_oneshot(desc, action);
	return ret;
}

/* wake_threads_waitq inlined - just atomic_dec_and_test + wake_up */

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
		if (atomic_dec_and_test(&desc->threads_active))
			wake_up(&desc->wait_for_threads);

	irq_finalize_oneshot(desc, action);
}

/* irq_wake_secondary inlined - single caller */

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

	/* irq_thread_set_ready inlined */
	set_bit(IRQTF_READY, &action->thread_flags);
	wake_up(&desc->wait_for_threads);

	sched_set_fifo(current);

	/* force_irqthreads() always false - simplified */
	handler_fn = irq_thread_fn;

	init_task_work(&on_exit_work, irq_thread_dtor);
	task_work_add(current, &on_exit_work, TWA_NONE);

	/* irq_thread_check_affinity calls removed - empty function */

	/* irq_wait_for_interrupt inlined */
	for (;;) {
		irqreturn_t action_ret;
		int should_exit;

		set_current_state(TASK_INTERRUPTIBLE);
		if (kthread_should_stop()) {
			if (test_and_clear_bit(IRQTF_RUNTHREAD,
					       &action->thread_flags)) {
				__set_current_state(TASK_RUNNING);
				should_exit = 0;
			} else {
				__set_current_state(TASK_RUNNING);
				should_exit = -1;
			}
		} else if (test_and_clear_bit(IRQTF_RUNTHREAD,
					      &action->thread_flags)) {
			__set_current_state(TASK_RUNNING);
			should_exit = 0;
		} else {
			schedule();
			continue;
		}
		if (should_exit)
			break;

		action_ret = handler_fn(desc, action);
		if (action_ret == IRQ_WAKE_THREAD) {
			struct irqaction *secondary = action->secondary;
			if (!WARN_ON_ONCE(!secondary)) {
				raw_spin_lock_irq(&desc->lock);
				__irq_wake_thread(desc, secondary);
				raw_spin_unlock_irq(&desc->lock);
			}
		}

		if (atomic_dec_and_test(&desc->threads_active))
			wake_up(&desc->wait_for_threads);
	}

	task_work_cancel(current, irq_thread_dtor);
	return 0;
}

/* irq_setup_forced_threading removed - always returned 0, caller check removed (~5 LOC) */
/* irq_request_resources and irq_release_resources inlined into __setup_irq */

static int setup_irq_thread(struct irqaction *new, unsigned int irq,
			    bool secondary)
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

static int __setup_irq(unsigned int irq, struct irq_desc *desc,
		       struct irqaction *new)
{
	struct irqaction *old, **old_ptr;
	unsigned long flags, thread_mask = 0;
	int ret, nested, shared = 0;

	if (!desc)
		return -EINVAL;

	if (desc->irq_data.chip == &no_irq_chip)
		return -ENOSYS;
	/* try_module_get always returns true - dead check removed */

	new->irq = irq;

	if (!(new->flags &IRQF_TRIGGER_MASK))
		new->flags |= irqd_get_trigger_type(&desc->irq_data);

	/* irq_settings_is_nested_thread inlined - single caller */
	nested = desc->status_use_accessors & _IRQ_NESTED_THREAD;
	if (nested) {
		if (!new->thread_fn) {
			ret = -EINVAL;
			goto out_mput;
		}

		new->handler = irq_nested_primary_handler;
	} else {
		/* irq_setup_forced_threading always returned 0, dead check removed */
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

	/* irq_request_resources removed - no chip sets this callback */

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
		    (oldtype != (new->flags &IRQF_TRIGGER_MASK)) ||
		    ((old->flags ^ new->flags) & IRQF_ONESHOT))
			goto mismatch;

		if ((old->flags & IRQF_PERCPU) != (new->flags &IRQF_PERCPU))
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
						new->flags &IRQF_TRIGGER_MASK);

			if (ret)
				goto out_unlock;
		}

		/* irq_activate always returns 0 - error check removed */
		irq_activate(desc);

		desc->istate &= ~(IRQS_AUTODETECT | IRQS_SPURIOUS_DISABLED |
				  IRQS_ONESHOT | IRQS_WAITING);
		irqd_clear(&desc->irq_data, IRQD_IRQ_INPROGRESS);

		if (new->flags & IRQF_PERCPU) {
			irqd_set(&desc->irq_data, IRQD_PER_CPU);
			desc->status_use_accessors |= _IRQ_PER_CPU;
			if (new->flags & IRQF_NO_DEBUG)
				irq_settings_set_no_debug(desc);
		}

		/* noirqdebug removed - always false */

		if (new->flags & IRQF_ONESHOT)
			desc->istate |= IRQS_ONESHOT;

		if (new->flags & IRQF_NOBALANCING) {
			irq_settings_set_no_balancing(desc);
			irqd_set(&desc->irq_data, IRQD_NO_BALANCING);
		}

		/* irq_settings_can_autoenable inlined - single caller */
		if (!(new->flags &IRQF_NO_AUTOEN) &&
		    !(desc->status_use_accessors & _IRQ_NOAUTOEN)) {
			irq_startup(desc, IRQ_RESEND, IRQ_START_COND);
		} else {
			WARN_ON_ONCE(new->flags &IRQF_SHARED);

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

	/* irq_pm_install_action removed - empty stub */
	desc->irq_count = 0;
	desc->irqs_unhandled = 0;

	if (shared && (desc->istate & IRQS_SPURIOUS_DISABLED)) {
		desc->istate &= ~IRQS_SPURIOUS_DISABLED;
		__enable_irq(desc);
	}

	raw_spin_unlock_irqrestore(&desc->lock, flags);
	chip_bus_sync_unlock(desc);
	mutex_unlock(&desc->request_mutex);

	/* irq_setup_timings removed - empty stub */
	wake_up_and_wait_for_irq_thread_ready(desc, new);
	wake_up_and_wait_for_irq_thread_ready(desc, new->secondary);

	/* register_irq_proc, register_handler_proc removed - empty stubs */
	return 0;

mismatch:
	if (!(new->flags &IRQF_PROBE_SHARED)) {
		pr_err("Flags mismatch irq %d. %08x (%s) vs. %08x (%s)\n", irq,
		       new->flags, new->name, old->flags, old->name);
	}
	ret = -EBUSY;

out_unlock:
	raw_spin_unlock_irqrestore(&desc->lock, flags);

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
	if (new->secondary &&new->secondary->thread) {
		struct task_struct *t = new->secondary->thread;

		new->secondary->thread = NULL;
		kthread_stop(t);
		put_task_struct(t);
	}
out_mput:
	module_put(desc->owner);
	return ret;
}

/* __free_irq/free_irq removed - never called */

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

	if ((desc->status_use_accessors & _IRQ_NOREQUEST) ||
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

	/* irq_chip_pm_get/irq_chip_pm_put removed - always returns 0 */

	retval = __setup_irq(irq, desc, action);

	if (retval) {
		kfree(action->secondary);
		kfree(action);
	}

	return retval;
}

/* __irq_get_irqchip_state, irq_get_irqchip_state, irq_set_irqchip_state,
 * irq_has_action, irq_check_status_bit removed - not called */
