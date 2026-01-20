
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
/* mm.h removed - unused */
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/module.h>
/* moduleparam.h, delay.h, memblock.h, ratelimit.h, kmsg_dump.h removed - unused */
#include <linux/smp.h>
#include <linux/syscalls.h>

#include <linux/cpu.h>
/* rculist.h, poll.h, irq_work.h, ctype.h, uio.h removed - unused */
/* sched/clock.h, sched/debug.h, sched/task_stack.h, uaccess.h removed - unused */
#include <asm/sections.h>

/* printk_ringbuffer.h structs, macros, and function declarations removed
   since all prb_* functions are now stubbed as macros below */
/* end printk_ringbuffer.h */
/* struct console_cmdline removed - never populated */
/* _braille_register_console removed - always returned 0 */
#include "internal.h"

int console_printk[4] = {
	CONSOLE_LOGLEVEL_DEFAULT,
	MESSAGE_LOGLEVEL_DEFAULT,
	CONSOLE_LOGLEVEL_MIN,
	CONSOLE_LOGLEVEL_DEFAULT,
};

atomic_t ignore_console_lock_warning __read_mostly = ATOMIC_INIT(0);

int oops_in_progress;

static DEFINE_SEMAPHORE(console_sem);
struct console *console_drivers;
/* suppress_panic_printk, nr_ext_console_drivers removed - write-only */

#define down_console_sem() down(&console_sem)

static int __down_trylock_console_sem(unsigned long ip)
{
	int lock_failed;
	unsigned long flags;

	printk_safe_enter_irqsave(flags);
	lock_failed = down_trylock(&console_sem);
	printk_safe_exit_irqrestore(flags);

	if (lock_failed)
		return 1;
	/* mutex_acquire removed - empty stub */
	return 0;
}
#define down_trylock_console_sem() __down_trylock_console_sem(_RET_IP_)

static void __up_console_sem(unsigned long ip)
{
	unsigned long flags;

	/* mutex_release removed - empty stub */

	printk_safe_enter_irqsave(flags);
	up(&console_sem);
	printk_safe_exit_irqrestore(flags);
}
#define up_console_sem() __up_console_sem(_RET_IP_)

/* panic_in_progress inlined into console_trylock */

static int console_locked, console_suspended;

/* console_cmdline array, preferred_console removed - never populated/used */

static int console_may_schedule;

/* console_msg_format, enum con_msg_format_flags removed - unused */

static DEFINE_MUTEX(syslog_lock);

/* CONSOLE_LOG_MAX, DROPPED_TEXT_MAX, printk_time removed - unused */

/* prb_read_valid, prb_first_valid_seq, prb_next_seq removed - inlined */

static u64 syslog_seq;
/* call_console_driver removed - was empty stub */

/* printk_console_no_auto_verbose removed - never set, condition simplified */

void console_verbose(void)
{
	if (console_loglevel)
		console_loglevel = CONSOLE_LOGLEVEL_MOTORMOUTH;
}

void console_lock(void)
{
	down_console_sem();
	if (console_suspended)
		return;
	console_locked = 1;
	console_may_schedule = 1;
}

int console_trylock(void)
{
	if (down_trylock_console_sem())
		return 0;
	if (console_suspended) {
		up_console_sem();
		return 0;
	}
	console_locked = 1;
	console_may_schedule = 0;
	return 1;
}

int is_console_locked(void)
{
	return console_locked;
}

/* console_is_usable inlined into console_flush_all */

/* console_emit_next_record removed - never called (~8 LOC) */
/* console_flush_all simplified - console_emit_next_record always returned false */
static bool console_flush_all(bool do_cond_resched, u64 *next_seq,
			      bool *handover)
{
	bool any_usable = false;
	struct console *con;

	*next_seq = 0;
	*handover = false;

	for_each_console(con) {
		/* console_is_usable inlined */
		if (!(con->flags & CON_ENABLED))
			continue;
		if (!con->write)
			continue;
		if (!cpu_online(raw_smp_processor_id()) &&
		    !(con->flags & CON_ANYTIME))
			continue;
		any_usable = true;
		if (con->seq > *next_seq)
			*next_seq = con->seq;
	}

	return any_usable;
}

void console_unlock(void)
{
	bool do_cond_resched;
	bool handover;
	u64 next_seq;

	if (console_suspended) {
		up_console_sem();
		return;
	}

	do_cond_resched = console_may_schedule;

	/* prb_read_valid always false - loop simplified to single iteration */
	console_may_schedule = 0;
	console_flush_all(do_cond_resched, &next_seq, &handover);
	if (!handover) {
		console_locked = 0;
		up_console_sem();
	}
}

void console_unblank(void)
{
	struct console *c;

	if (oops_in_progress) {
		if (down_trylock_console_sem() != 0)
			return;
	} else
		console_lock();

	console_locked = 1;
	console_may_schedule = 0;
	for_each_console(c)
		if ((c->flags & CON_ENABLED) && c->unblank)
			c->unblank();
	console_unlock();

	/* pr_flush removed - always returns true, return value unused */
}

/* console_flush_on_panic simplified - only called with CONSOLE_FLUSH_PENDING */
void console_flush_on_panic(enum con_flush_mode mode)
{
	console_trylock();
	console_may_schedule = 0;
	console_unlock();
}

struct tty_driver *console_device(int *index)
{
	struct console *c;
	struct tty_driver *driver = NULL;

	console_lock();
	for_each_console(c) {
		if (!c->device)
			continue;
		driver = c->device(c, index);
		if (driver)
			break;
	}
	console_unlock();
	return driver;
}

/* keep_bootcon removed - never set, always false */

/* try_enable_preferred_console simplified - console_cmdline never populated */
static int try_enable_preferred_console(struct console *newcon,
					bool user_specified)
{
	/* console_cmdline array is never populated, so loop never executes */
	if (newcon->flags & CON_ENABLED)
		return 0;
	return -ENOENT;
}

/* try_enable_default_console inlined into register_console */

#define con_printk(lvl, con, fmt, ...)                                       \
	printk(lvl pr_fmt("%sconsole [%s%d] " fmt),                          \
	       (con->flags & CON_BOOT) ? "boot" : "", con->name, con->index, \
	       ##__VA_ARGS__)

void register_console(struct console *newcon)
{
	struct console *con;
	bool bootcon_enabled = false;
	bool realcon_enabled = false;
	int err;

	for_each_console(con) {
		if (WARN(con == newcon, "console '%s%d' already registered\n",
			 con->name, con->index))
			return;
	}

	for_each_console(con) {
		if (con->flags & CON_BOOT)
			bootcon_enabled = true;
		else
			realcon_enabled = true;
	}

	if (newcon->flags & CON_BOOT && realcon_enabled) {
		return;
	}

	/* preferred_console always -1, so condition always true */
	if (!console_drivers || !console_drivers->device ||
	    console_drivers->flags & CON_BOOT) {
		/* inlined try_enable_default_console */
		if (newcon->index < 0)
			newcon->index = 0;
		if (!newcon->setup || newcon->setup(newcon, NULL) == 0) {
			newcon->flags |= CON_ENABLED;
			if (newcon->device)
				newcon->flags |= CON_CONSDEV;
		}
	}

	err = try_enable_preferred_console(newcon, true);

	if (err == -ENOENT)
		err = try_enable_preferred_console(newcon, false);

	if (err || newcon->flags & CON_BRL)
		return;

	if (bootcon_enabled &&
	    ((newcon->flags & (CON_CONSDEV | CON_BOOT)) == CON_CONSDEV)) {
		newcon->flags &= ~CON_PRINTBUFFER;
	}

	console_lock();
	if ((newcon->flags & CON_CONSDEV) || console_drivers == NULL) {
		newcon->next = console_drivers;
		console_drivers = newcon;
		if (newcon->next)
			newcon->next->flags &= ~CON_CONSDEV;

		newcon->flags |= CON_CONSDEV;
	} else {
		newcon->next = console_drivers->next;
		console_drivers->next = newcon;
	}

	newcon->dropped = 0;
	if (newcon->flags & CON_PRINTBUFFER) {
		mutex_lock(&syslog_lock);
		newcon->seq = syslog_seq;
		mutex_unlock(&syslog_lock);
	} else {
		newcon->seq = 0; /* prb_next_seq always 0 */
	}
	console_unlock();
	/* console_sysfs_notify call removed - empty stub */
	con_printk(KERN_INFO, newcon, "enabled\n");
	/* keep_bootcon check removed - never set, always unregister boot consoles */
	if (bootcon_enabled &&
	    ((newcon->flags & (CON_CONSDEV | CON_BOOT)) == CON_CONSDEV)) {
		for_each_console(con)
			if (con->flags & CON_BOOT)
				unregister_console(con);
	}
}

int unregister_console(struct console *console)
{
	/* Stub: console unregistration not needed for minimal kernel */
	return 0;
}

void __init console_init(void)
{
	int ret;
	initcall_t call;
	initcall_entry_t *ce;

	n_tty_init();

	ce = __con_initcall_start;

	while (ce < __con_initcall_end) {
		call = initcall_from_entry(ce);

		ret = call();

		ce++;
	}
}
