#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/poll.h>
/* proc_fs.h removed - empty header */
/* linux/module.h removed - no module features used */
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>
#include <linux/ratelimit.h>
#include "tty.h"

/* tty_ldisc_debug removed - was empty macro */

enum {
	LDISC_SEM_NORMAL,
	LDISC_SEM_OTHER,
};

static DEFINE_RAW_SPINLOCK(tty_ldiscs_lock);
static struct tty_ldisc_ops *tty_ldiscs[NR_LDISCS];

int tty_register_ldisc(struct tty_ldisc_ops *new_ldisc)
{
	unsigned long flags;

	if (new_ldisc->num < N_TTY || new_ldisc->num >= NR_LDISCS)
		return -EINVAL;

	raw_spin_lock_irqsave(&tty_ldiscs_lock, flags);
	tty_ldiscs[new_ldisc->num] = new_ldisc;
	raw_spin_unlock_irqrestore(&tty_ldiscs_lock, flags);

	return 0;
}

/* tty_unregister_ldisc stubbed - ldisc modules never unloaded in minimal kernel */
/* get_ldops inlined into tty_ldisc_get */

static struct tty_ldisc *tty_ldisc_get(struct tty_struct *tty, int disc)
{
	struct tty_ldisc *ld;
	struct tty_ldisc_ops *ldops;
	unsigned long flags;

	if (disc < N_TTY || disc >= NR_LDISCS)
		return ERR_PTR(-EINVAL);

	/* Inlined get_ldops */
	raw_spin_lock_irqsave(&tty_ldiscs_lock, flags);
	ldops = tty_ldiscs[disc];
	raw_spin_unlock_irqrestore(&tty_ldiscs_lock, flags);
	if (!ldops)
		return ERR_PTR(-EINVAL);

	ld = kmalloc(sizeof(struct tty_ldisc), GFP_KERNEL | __GFP_NOFAIL);
	ld->ops = ldops;
	ld->tty = tty;

	return ld;
}

static void tty_ldisc_put(struct tty_ldisc *ld)
{
	if (WARN_ON_ONCE(!ld))
		return;

	unsigned long flags;
	raw_spin_lock_irqsave(&tty_ldiscs_lock, flags);
	module_put(ld->ops->owner);
	raw_spin_unlock_irqrestore(&tty_ldiscs_lock, flags);
	kfree(ld);
}

struct tty_ldisc *tty_ldisc_ref_wait(struct tty_struct *tty)
{
	struct tty_ldisc *ld;

	ldsem_down_read(&tty->ldisc_sem, MAX_SCHEDULE_TIMEOUT);
	ld = tty->ldisc;
	if (!ld)
		ldsem_up_read(&tty->ldisc_sem);
	return ld;
}

struct tty_ldisc *tty_ldisc_ref(struct tty_struct *tty)
{
	struct tty_ldisc *ld = NULL;

	if (ldsem_down_read_trylock(&tty->ldisc_sem)) {
		ld = tty->ldisc;
		if (!ld)
			ldsem_up_read(&tty->ldisc_sem);
	}
	return ld;
}

void tty_ldisc_deref(struct tty_ldisc *ld)
{
	ldsem_up_read(&ld->tty->ldisc_sem);
}

static inline int __tty_ldisc_lock(struct tty_struct *tty,
				   unsigned long timeout)
{
	return ldsem_down_write(&tty->ldisc_sem, timeout);
}

static inline int __tty_ldisc_lock_nested(struct tty_struct *tty,
					  unsigned long timeout)
{
	return ldsem_down_write_nested(&tty->ldisc_sem, LDISC_SEM_OTHER,
				       timeout);
}

static inline void __tty_ldisc_unlock(struct tty_struct *tty)
{
	ldsem_up_write(&tty->ldisc_sem);
}

int tty_ldisc_lock(struct tty_struct *tty, unsigned long timeout)
{
	int ret;

	set_bit(TTY_LDISC_CHANGING, &tty->flags);
	wake_up_interruptible_all(&tty->read_wait);
	wake_up_interruptible_all(&tty->write_wait);

	ret = __tty_ldisc_lock(tty, timeout);
	if (!ret)
		return -EBUSY;
	set_bit(TTY_LDISC_HALTED, &tty->flags);
	return 0;
}

void tty_ldisc_unlock(struct tty_struct *tty)
{
	clear_bit(TTY_LDISC_HALTED, &tty->flags);

	clear_bit(TTY_LDISC_CHANGING, &tty->flags);
	__tty_ldisc_unlock(tty);
}

/* tty_ldisc_lock_pair_timeout inlined into tty_ldisc_release */
/* tty_ldisc_flush removed - never called */
/* tty_set_termios_ldisc inlined into tty_set_ldisc */

static int tty_ldisc_open(struct tty_struct *tty, struct tty_ldisc *ld)
{
	WARN_ON(test_and_set_bit(TTY_LDISC_OPEN, &tty->flags));
	if (ld->ops->open) {
		int ret;

		ret = ld->ops->open(tty);
		if (ret)
			clear_bit(TTY_LDISC_OPEN, &tty->flags);

		return ret;
	}
	return 0;
}

static void tty_ldisc_close(struct tty_struct *tty, struct tty_ldisc *ld)
{
	WARN_ON(!test_bit(TTY_LDISC_OPEN, &tty->flags));
	clear_bit(TTY_LDISC_OPEN, &tty->flags);
	if (ld->ops->close)
		ld->ops->close(tty);
}

static void tty_ldisc_kill(struct tty_struct *tty)
{
	if (!tty->ldisc)
		return;

	tty_ldisc_close(tty, tty->ldisc);
	tty_ldisc_put(tty->ldisc);

	tty->ldisc = NULL;
}

int tty_ldisc_reinit(struct tty_struct *tty, int disc)
{
	struct tty_ldisc *ld;
	int retval;

	ld = tty_ldisc_get(tty, disc);
	if (IS_ERR(ld)) {
		BUG_ON(disc == N_TTY);
		return PTR_ERR(ld);
	}

	if (tty->ldisc) {
		tty_ldisc_close(tty, tty->ldisc);
		tty_ldisc_put(tty->ldisc);
	}

	tty->ldisc = ld;
	/* tty_set_termios_ldisc inlined */
	down_write(&tty->termios_rwsem);
	tty->termios.c_line = disc;
	up_write(&tty->termios_rwsem);
	tty->disc_data = NULL;
	/* tty->receive_room assignment removed - field was write-only */

	retval = tty_ldisc_open(tty, tty->ldisc);
	if (retval) {
		tty_ldisc_put(tty->ldisc);
		tty->ldisc = NULL;
	}
	return retval;
}

/* tty_ldisc_hangup removed - never called (~36 LOC) */

int tty_ldisc_setup(struct tty_struct *tty, struct tty_struct *o_tty)
{
	int retval = tty_ldisc_open(tty, tty->ldisc);

	if (retval)
		return retval;

	if (o_tty) {
		retval = tty_ldisc_open(o_tty, o_tty->ldisc);
		if (retval) {
			tty_ldisc_close(tty, tty->ldisc);
			return retval;
		}
	}
	return 0;
}

void tty_ldisc_release(struct tty_struct *tty)
{
	struct tty_struct *o_tty = tty->link;
	int ret;

	/* tty_ldisc_lock_pair_timeout inlined */
	if (tty < o_tty) {
		ret = __tty_ldisc_lock(tty, MAX_SCHEDULE_TIMEOUT);
		if (ret) {
			ret = __tty_ldisc_lock_nested(o_tty,
						      MAX_SCHEDULE_TIMEOUT);
			if (!ret)
				__tty_ldisc_unlock(tty);
		}
	} else {
		WARN_ON_ONCE(tty == o_tty);
		if (o_tty && tty != o_tty) {
			ret = __tty_ldisc_lock(o_tty, MAX_SCHEDULE_TIMEOUT);
			if (ret) {
				ret = __tty_ldisc_lock_nested(
					tty, MAX_SCHEDULE_TIMEOUT);
				if (!ret)
					__tty_ldisc_unlock(o_tty);
			}
		} else
			ret = __tty_ldisc_lock(tty, MAX_SCHEDULE_TIMEOUT);
	}
	if (ret) {
		set_bit(TTY_LDISC_HALTED, &tty->flags);
		if (o_tty)
			set_bit(TTY_LDISC_HALTED, &o_tty->flags);
	}

	tty_ldisc_kill(tty);
	if (o_tty)
		tty_ldisc_kill(o_tty);
	__tty_ldisc_unlock(tty);
	if (o_tty)
		__tty_ldisc_unlock(o_tty);
}

int tty_ldisc_init(struct tty_struct *tty)
{
	struct tty_ldisc *ld = tty_ldisc_get(tty, N_TTY);

	if (IS_ERR(ld))
		return PTR_ERR(ld);
	tty->ldisc = ld;
	return 0;
}

void tty_ldisc_deinit(struct tty_struct *tty)
{
	if (tty->ldisc)
		tty_ldisc_put(tty->ldisc);
	tty->ldisc = NULL;
}

/* tty_sysctl_init removed - empty stub, call removed from tty_io.c */
