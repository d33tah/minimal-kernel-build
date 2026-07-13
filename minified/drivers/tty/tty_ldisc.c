#include <linux/sched.h>
#include <linux/tty.h>
#include "tty.h"

#define tty_ldisc_debug(tty, f, args...)



static DEFINE_RAW_SPINLOCK(tty_ldiscs_lock);
static struct tty_ldisc_ops *tty_ldiscs[NR_LDISCS];

int tty_register_ldisc(struct tty_ldisc_ops *new_ldisc)
{
	unsigned long flags;
	int ret = 0;

	if (new_ldisc->num < N_TTY || new_ldisc->num >= NR_LDISCS)
		return -EINVAL;

	raw_spin_lock_irqsave(&tty_ldiscs_lock, flags);
	tty_ldiscs[new_ldisc->num] = new_ldisc;
	raw_spin_unlock_irqrestore(&tty_ldiscs_lock, flags);

	return ret;
}


/* tty_unregister_ldisc stubbed - ldisc modules never unloaded in minimal kernel */

static struct tty_ldisc_ops *get_ldops(int disc)
{
	unsigned long flags;
	struct tty_ldisc_ops *ldops, *ret;

	raw_spin_lock_irqsave(&tty_ldiscs_lock, flags);
	ret = ERR_PTR(-EINVAL);
	ldops = tty_ldiscs[disc];
	if (ldops) {
		ret = ldops;
	}
	raw_spin_unlock_irqrestore(&tty_ldiscs_lock, flags);
	return ret;
}

static struct tty_ldisc *tty_ldisc_get(struct tty_struct *tty, int disc)
{
	struct tty_ldisc *ld;
	struct tty_ldisc_ops *ldops;

	if (disc < N_TTY || disc >= NR_LDISCS)
		return ERR_PTR(-EINVAL);

	/*
	 * Modules are off and request_module() is a no-op (-ENOSYS), so the
	 * ldisc-autoload retry path could never register a new ldisc; an
	 * unregistered disc just fails here.
	 */
	ldops = get_ldops(disc);
	if (IS_ERR(ldops))
		return ERR_CAST(ldops);

	 
	ld = kmalloc(sizeof(struct tty_ldisc), GFP_KERNEL | __GFP_NOFAIL);
	ld->ops = ldops;
	ld->tty = tty;

	return ld;
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

void tty_ldisc_deref(struct tty_ldisc *ld)
{
	ldsem_up_read(&ld->tty->ldisc_sem);
}


static inline int __tty_ldisc_lock(struct tty_struct *tty, unsigned long timeout)
{
	return ldsem_down_write(&tty->ldisc_sem, timeout);
}

static inline void __tty_ldisc_unlock(struct tty_struct *tty)
{
	ldsem_up_write(&tty->ldisc_sem);
}

int tty_ldisc_lock(struct tty_struct *tty, unsigned long timeout)
{
	int ret;


	wake_up_interruptible_all(&tty->read_wait);
	wake_up_interruptible_all(&tty->write_wait);

	ret = __tty_ldisc_lock(tty, timeout);
	return !ret ? -EBUSY : 0;
}

void tty_ldisc_unlock(struct tty_struct *tty)
{
	__tty_ldisc_unlock(tty);
}

static int tty_ldisc_open(struct tty_struct *tty, struct tty_ldisc *ld)
{
	WARN_ON(test_and_set_bit(TTY_LDISC_OPEN, &tty->flags));
	if (ld->ops->open) {
		int ret;
		 
		ret = ld->ops->open(tty);
		if (ret)
			clear_bit(TTY_LDISC_OPEN, &tty->flags);

		tty_ldisc_debug(tty, "%p: opened\n", ld);
		return ret;
	}
	return 0;
}

int tty_ldisc_reinit(struct tty_struct *tty, int disc)
{
	/* Runtime-dead: only caller is tty_reopen (re-opening an already-open
	 * tty), which never runs on a single-shot boot. Link-live via tty.h
	 * extern. */
	return -EINVAL;
}

int tty_ldisc_setup(struct tty_struct *tty, struct tty_struct *o_tty)
{
	/* o_tty (tty->link) is only non-NULL for a PTY pair; this build has no
	 * PTY driver, so the link is always NULL and the o_tty open/close path
	 * (the sole tty_ldisc_close caller, hence the sole ld->ops->close
	 * dereference) is statically dead. Only the primary ldisc is opened. */
	return tty_ldisc_open(tty, tty->ldisc);
}

int tty_ldisc_init(struct tty_struct *tty)
{
	struct tty_ldisc *ld = tty_ldisc_get(tty, N_TTY);

	if (IS_ERR(ld))
		return PTR_ERR(ld);
	tty->ldisc = ld;
	return 0;
}

