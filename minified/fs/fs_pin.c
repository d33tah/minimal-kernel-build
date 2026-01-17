#include <linux/fs.h>
#include <linux/sched.h>
#include "internal.h"
#include "mount.h"

/* pin_lock, pin_remove, pin_insert removed - never called */

void pin_kill(struct fs_pin *p)
{
	wait_queue_entry_t wait;

	if (!p) {
		rcu_read_unlock();
		return;
	}
	init_wait(&wait);
	spin_lock_irq(&p->wait.lock);
	if (likely(!p->done)) {
		p->done = -1;
		spin_unlock_irq(&p->wait.lock);
		rcu_read_unlock();
		p->kill(p);
		return;
	}
	if (p->done > 0) {
		spin_unlock_irq(&p->wait.lock);
		rcu_read_unlock();
		return;
	}
	__add_wait_queue(&p->wait, &wait);
	while (1) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		spin_unlock_irq(&p->wait.lock);
		rcu_read_unlock();
		schedule();
		rcu_read_lock();
		if (likely(list_empty(&wait.entry)))
			break;

		spin_lock_irq(&p->wait.lock);
		if (p->done > 0) {
			spin_unlock_irq(&p->wait.lock);
			break;
		}
	}
	rcu_read_unlock();
}

void mnt_pin_kill(struct mount *m)
{
	while (1) {
		struct hlist_node *p;
		rcu_read_lock();
		p = READ_ONCE(m->mnt_pins.first);
		if (!p) {
			rcu_read_unlock();
			break;
		}
		pin_kill(hlist_entry(p, struct fs_pin, m_list));
	}
}
