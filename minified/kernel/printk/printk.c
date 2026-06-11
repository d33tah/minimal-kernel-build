
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/nmi.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/memblock.h>
#include <linux/syscalls.h>
#include <linux/ratelimit.h>
#include <linux/kmsg_dump.h>

#include <linux/cpu.h>
#include <linux/rculist.h>
#include <linux/poll.h>
#include <linux/irq_work.h>
#include <linux/ctype.h>
#include <linux/uio.h>
#include <linux/sched/clock.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>

#include <linux/uaccess.h>
#include <asm/sections.h>



/* The printk ring buffer (printk_ringbuffer.h structs/macros) was removed:
 * CONFIG_PRINTK is unset, so prb_read_valid()/prb_next_seq()/prb_first_valid_seq()
 * are constant stubs, no struct printk_ringbuffer is ever instantiated, and the
 * record-emit path (console_emit_next_record) never reads a record. */
struct console_cmdline { char name[16]; int index; bool user_specified; char *options; };
/* end console_cmdline.h */
static inline int _braille_register_console(struct console *console, struct console_cmdline *c) { return 0; }
/* end braille.h */
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



#define down_console_sem() do { \
	down(&console_sem);\
	mutex_acquire(&console_lock_dep_map, 0, 0, _RET_IP_);\
} while (0)

static int __down_trylock_console_sem(unsigned long ip)
{
	int lock_failed;
	unsigned long flags;

	 
	printk_safe_enter_irqsave(flags);
	lock_failed = down_trylock(&console_sem);
	printk_safe_exit_irqrestore(flags);

	if (lock_failed)
		return 1;
	mutex_acquire(&console_lock_dep_map, 0, 1, ip);
	return 0;
}
#define down_trylock_console_sem() __down_trylock_console_sem(_RET_IP_)

static void __up_console_sem(unsigned long ip)
{
	unsigned long flags;

	mutex_release(&console_lock_dep_map, ip);

	printk_safe_enter_irqsave(flags);
	up(&console_sem);
	printk_safe_exit_irqrestore(flags);
}
#define up_console_sem() __up_console_sem(_RET_IP_)

static bool panic_in_progress(void)
{
	return unlikely(atomic_read(&panic_cpu) != PANIC_CPU_INVALID);
}

static int console_locked;


#define MAX_CMDLINECONSOLES 8

static struct console_cmdline console_cmdline[MAX_CMDLINECONSOLES];

static int preferred_console = -1;

static int console_may_schedule;

static DEFINE_MUTEX(syslog_lock);


/* No ring buffer on this !CONFIG_PRINTK build: reads always fail. */
#define prb_read_valid(rb, seq, r)	false
#define prb_first_valid_seq(rb)		0
#define prb_next_seq(rb)		0

static u64 syslog_seq;






static bool printk_console_no_auto_verbose;

void console_verbose(void)
{
	if (console_loglevel && !printk_console_no_auto_verbose)
		console_loglevel = CONSOLE_LOGLEVEL_MOTORMOUTH;
}

void console_lock(void)
{
	might_sleep();

	down_console_sem();
	console_locked = 1;
	console_may_schedule = 1;
}

int console_trylock(void)
{
	if (down_trylock_console_sem())
		return 0;
	console_locked = 1;
	console_may_schedule = 0;
	return 1;
}

int is_console_locked(void)
{
	return console_locked;
}

static bool abandon_console_lock_in_panic(void)
{
	if (!panic_in_progress())
		return false;

	 
	return atomic_read(&panic_cpu) != raw_smp_processor_id();
}

static inline bool console_is_usable(struct console *con)
{
	if (!(con->flags & CON_ENABLED))
		return false;

	if (!con->write)
		return false;

	 
	if (!cpu_online(raw_smp_processor_id()) &&
	    !(con->flags & CON_ANYTIME))
		return false;

	return true;
}

static void __console_unlock(void)
{
	console_locked = 0;
	up_console_sem();
}

static bool console_emit_next_record(struct console *con, bool *handover)
{
	/* prb_read_valid() is a constant `false` stub on this !CONFIG_PRINTK
	 * build (no ring buffer), so there is never a record to emit. */
	*handover = false;
	return false;
}

static bool console_flush_all(bool do_cond_resched, u64 *next_seq, bool *handover)
{
	bool any_usable = false;
	struct console *con;
	bool any_progress;

	*next_seq = 0;
	*handover = false;

	do {
		any_progress = false;

		for_each_console(con) {
			bool progress;

			if (!console_is_usable(con))
				continue;
			any_usable = true;

			progress = console_emit_next_record(con, handover);
			if (*handover)
				return false;

			 
			if (con->seq > *next_seq)
				*next_seq = con->seq;

			if (!progress)
				continue;
			any_progress = true;

			 
			if (abandon_console_lock_in_panic())
				return false;

			if (do_cond_resched)
				cond_resched();
		}
	} while (any_progress);

	return any_usable;
}

void console_unlock(void)
{
	bool do_cond_resched;
	bool handover;
	bool flushed;
	u64 next_seq;


	do_cond_resched = console_may_schedule;

	do {
		console_may_schedule = 0;

		flushed = console_flush_all(do_cond_resched, &next_seq, &handover);
		if (!handover)
			__console_unlock();

		 
		if (!flushed)
			break;

		 
	} while (prb_read_valid(prb, next_seq, NULL) && console_trylock());
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

	if (!oops_in_progress)
		pr_flush(1000, true);
}

void console_flush_on_panic(enum con_flush_mode mode)
{
	 
	console_trylock();
	console_may_schedule = 0;

	if (mode == CONSOLE_REPLAY_ALL) {
		struct console *c;
		u64 seq;

		seq = prb_first_valid_seq(prb);
		for_each_console(c)
			c->seq = seq;
	}
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


static int __read_mostly keep_bootcon;

static int try_enable_preferred_console(struct console *newcon,
					bool user_specified)
{
	struct console_cmdline *c;
	int i, err;

	for (i = 0, c = console_cmdline;
	     i < MAX_CMDLINECONSOLES && c->name[0];
	     i++, c++) {
		if (c->user_specified != user_specified)
			continue;
		if (!newcon->match ||
		    newcon->match(newcon, c->name, c->index, c->options) != 0) {
			 
			BUILD_BUG_ON(sizeof(c->name) != sizeof(newcon->name));
			if (strcmp(c->name, newcon->name) != 0)
				continue;
			if (newcon->index >= 0 &&
			    newcon->index != c->index)
				continue;
			if (newcon->index < 0)
				newcon->index = c->index;

			if (_braille_register_console(newcon, c))
				return 0;

			if (newcon->setup &&
			    (err = newcon->setup(newcon, c->options)) != 0)
				return err;
		}
		newcon->flags |= CON_ENABLED;
		if (i == preferred_console)
			newcon->flags |= CON_CONSDEV;
		return 0;
	}

	 
	if (newcon->flags & CON_ENABLED && c->user_specified ==	user_specified)
		return 0;

	return -ENOENT;
}

static void try_enable_default_console(struct console *newcon)
{
	if (newcon->index < 0)
		newcon->index = 0;

	if (newcon->setup && newcon->setup(newcon, NULL) != 0)
		return;

	newcon->flags |= CON_ENABLED;

	if (newcon->device)
		newcon->flags |= CON_CONSDEV;
}

#define con_printk(lvl, con, fmt, ...)			\
	printk(lvl pr_fmt("%sconsole [%s%d] " fmt),	\
	       (con->flags & CON_BOOT) ? "boot" : "",	\
	       con->name, con->index, ##__VA_ARGS__)

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

	 
	if (preferred_console < 0) {
		if (!console_drivers || !console_drivers->device ||
		    console_drivers->flags & CON_BOOT) {
			try_enable_default_console(newcon);
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
		 
		newcon->seq = prb_next_seq(prb);
	}
	console_unlock();
	console_sysfs_notify();

	 
	con_printk(KERN_INFO, newcon, "enabled\n");
	if (bootcon_enabled &&
	    ((newcon->flags & (CON_CONSDEV | CON_BOOT)) == CON_CONSDEV) &&
	    !keep_bootcon) {
		 
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



