 
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kmod.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/ratelimit.h>
#include "tty.h"

#undef LDISC_DEBUG_HANGUP

#ifdef LDISC_DEBUG_HANGUP
#define tty_ldisc_debug(tty, f, args...)	tty_debug(tty, f, ##args)
#else
#define tty_ldisc_debug(tty, f, args...)
#endif

 
enum {
	LDISC_SEM_NORMAL,
	LDISC_SEM_OTHER,
};


 

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

 

void tty_unregister_ldisc(struct tty_ldisc_ops *ldisc)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&tty_ldiscs_lock, flags);
	tty_ldiscs[ldisc->num] = NULL;
	raw_spin_unlock_irqrestore(&tty_ldiscs_lock, flags);
}

static struct tty_ldisc_ops *get_ldops(int disc)
{
	unsigned long flags;
	struct tty_ldisc_ops *ldops, *ret;

	raw_spin_lock_irqsave(&tty_ldiscs_lock, flags);
	ret = ERR_PTR(-EINVAL);
	ldops = tty_ldiscs[disc];
	if (ldops) {
		ret = ERR_PTR(-EAGAIN);
		if (try_module_get(ldops->owner))
			ret = ldops;
	}
	raw_spin_unlock_irqrestore(&tty_ldiscs_lock, flags);
	return ret;
}

static void put_ldops(struct tty_ldisc_ops *ldops)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&tty_ldiscs_lock, flags);
	module_put(ldops->owner);
	raw_spin_unlock_irqrestore(&tty_ldiscs_lock, flags);
}

static int tty_ldisc_autoload = IS_BUILTIN(CONFIG_LDISC_AUTOLOAD);

 
static struct tty_ldisc *tty_ldisc_get(struct tty_struct *tty, int disc)
{
	struct tty_ldisc *ld;
	struct tty_ldisc_ops *ldops;

	if (disc < N_TTY || disc >= NR_LDISCS)
		return ERR_PTR(-EINVAL);

	 
	ldops = get_ldops(disc);
	if (IS_ERR(ldops)) {
		if (!capable(CAP_SYS_MODULE) && !tty_ldisc_autoload)
			return ERR_PTR(-EPERM);
		request_module("tty-ldisc-%d", disc);
		ldops = get_ldops(disc);
		if (IS_ERR(ldops))
			return ERR_CAST(ldops);
	}

	 
	ld = kmalloc(sizeof(struct tty_ldisc), GFP_KERNEL | __GFP_NOFAIL);
	ld->ops = ldops;
	ld->tty = tty;

	return ld;
}

 
static void tty_ldisc_put(struct tty_ldisc *ld)
{
	if (WARN_ON_ONCE(!ld))
		return;

	put_ldops(ld->ops);
	kfree(ld);
}

static void *tty_ldiscs_seq_start(struct seq_file *m, loff_t *pos)
{
	/* Stub: /proc/tty/ldiscs not needed for minimal kernel */
	return NULL;
}

static void *tty_ldiscs_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	return NULL;
}

static void tty_ldiscs_seq_stop(struct seq_file *m, void *v)
{
}

static int tty_ldiscs_seq_show(struct seq_file *m, void *v)
{
	return 0;
}

const struct seq_operations tty_ldiscs_seq_ops = {
	.start	= tty_ldiscs_seq_start,
	.next	= tty_ldiscs_seq_next,
	.stop	= tty_ldiscs_seq_stop,
	.show	= tty_ldiscs_seq_show,
};

 
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


static inline int
__tty_ldisc_lock(struct tty_struct *tty, unsigned long timeout)
{
	return ldsem_down_write(&tty->ldisc_sem, timeout);
}

static inline int
__tty_ldisc_lock_nested(struct tty_struct *tty, unsigned long timeout)
{
	return ldsem_down_write_nested(&tty->ldisc_sem,
				       LDISC_SEM_OTHER, timeout);
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

static int
tty_ldisc_lock_pair_timeout(struct tty_struct *tty, struct tty_struct *tty2,
			    unsigned long timeout)
{
	int ret;

	if (tty < tty2) {
		ret = __tty_ldisc_lock(tty, timeout);
		if (ret) {
			ret = __tty_ldisc_lock_nested(tty2, timeout);
			if (!ret)
				__tty_ldisc_unlock(tty);
		}
	} else {
		 
		WARN_ON_ONCE(tty == tty2);
		if (tty2 && tty != tty2) {
			ret = __tty_ldisc_lock(tty2, timeout);
			if (ret) {
				ret = __tty_ldisc_lock_nested(tty, timeout);
				if (!ret)
					__tty_ldisc_unlock(tty2);
			}
		} else
			ret = __tty_ldisc_lock(tty, timeout);
	}

	if (!ret)
		return -EBUSY;

	set_bit(TTY_LDISC_HALTED, &tty->flags);
	if (tty2)
		set_bit(TTY_LDISC_HALTED, &tty2->flags);
	return 0;
}

static void tty_ldisc_lock_pair(struct tty_struct *tty, struct tty_struct *tty2)
{
	tty_ldisc_lock_pair_timeout(tty, tty2, MAX_SCHEDULE_TIMEOUT);
}

static void tty_ldisc_unlock_pair(struct tty_struct *tty,
				  struct tty_struct *tty2)
{
	__tty_ldisc_unlock(tty);
	if (tty2)
		__tty_ldisc_unlock(tty2);
}

 
void tty_ldisc_flush(struct tty_struct *tty)
{
	struct tty_ldisc *ld = tty_ldisc_ref(tty);

	tty_buffer_flush(tty, ld);
	if (ld)
		tty_ldisc_deref(ld);
}

 
static void tty_set_termios_ldisc(struct tty_struct *tty, int disc)
{
	down_write(&tty->termios_rwsem);
	tty->termios.c_line = disc;
	up_write(&tty->termios_rwsem);

	tty->disc_data = NULL;
	tty->receive_room = 0;
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

 
static void tty_ldisc_close(struct tty_struct *tty, struct tty_ldisc *ld)
{
	lockdep_assert_held_write(&tty->ldisc_sem);
	WARN_ON(!test_bit(TTY_LDISC_OPEN, &tty->flags));
	clear_bit(TTY_LDISC_OPEN, &tty->flags);
	if (ld->ops->close)
		ld->ops->close(tty);
	tty_ldisc_debug(tty, "%p: closed\n", ld);
}

/* tty_ldisc_failto removed - unused */

/* Stubbed - ldisc switching not needed for minimal kernel */
int tty_set_ldisc(struct tty_struct *tty, int disc)
{
	return -EINVAL;
}

 
static void tty_ldisc_kill(struct tty_struct *tty)
{
	lockdep_assert_held_write(&tty->ldisc_sem);
	if (!tty->ldisc)
		return;
	 
	tty_ldisc_close(tty, tty->ldisc);
	tty_ldisc_put(tty->ldisc);
	 
	tty->ldisc = NULL;
}

 
static void tty_reset_termios(struct tty_struct *tty)
{
	down_write(&tty->termios_rwsem);
	tty->termios = tty->driver->init_termios;
	tty->termios.c_ispeed = tty_termios_input_baud_rate(&tty->termios);
	tty->termios.c_ospeed = tty_termios_baud_rate(&tty->termios);
	up_write(&tty->termios_rwsem);
}


 
int tty_ldisc_reinit(struct tty_struct *tty, int disc)
{
	struct tty_ldisc *ld;
	int retval;

	lockdep_assert_held_write(&tty->ldisc_sem);
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
	tty_set_termios_ldisc(tty, disc);
	retval = tty_ldisc_open(tty, tty->ldisc);
	if (retval) {
		tty_ldisc_put(tty->ldisc);
		tty->ldisc = NULL;
	}
	return retval;
}

 
void tty_ldisc_hangup(struct tty_struct *tty, bool reinit)
{
	struct tty_ldisc *ld;

	tty_ldisc_debug(tty, "%p: hangup\n", tty->ldisc);

	ld = tty_ldisc_ref(tty);
	if (ld != NULL) {
		if (ld->ops->flush_buffer)
			ld->ops->flush_buffer(tty);
		tty_driver_flush_buffer(tty);
		if ((test_bit(TTY_DO_WRITE_WAKEUP, &tty->flags)) &&
		    ld->ops->write_wakeup)
			ld->ops->write_wakeup(tty);
		if (ld->ops->hangup)
			ld->ops->hangup(tty);
		tty_ldisc_deref(ld);
	}

	wake_up_interruptible_poll(&tty->write_wait, EPOLLOUT);
	wake_up_interruptible_poll(&tty->read_wait, EPOLLIN);

	 
	tty_ldisc_lock(tty, MAX_SCHEDULE_TIMEOUT);

	if (tty->driver->flags & TTY_DRIVER_RESET_TERMIOS)
		tty_reset_termios(tty);

	if (tty->ldisc) {
		if (reinit) {
			if (tty_ldisc_reinit(tty, tty->termios.c_line) < 0 &&
			    tty_ldisc_reinit(tty, N_TTY) < 0)
				WARN_ON(tty_ldisc_reinit(tty, N_NULL) < 0);
		} else
			tty_ldisc_kill(tty);
	}
	tty_ldisc_unlock(tty);
}

 
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

	 

	tty_ldisc_lock_pair(tty, o_tty);
	tty_ldisc_kill(tty);
	if (o_tty)
		tty_ldisc_kill(o_tty);
	tty_ldisc_unlock_pair(tty, o_tty);

	 

	tty_ldisc_debug(tty, "released\n");
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

/* Stub: tty sysctl entries not needed for minimal kernel */
void tty_sysctl_init(void)
{
}
