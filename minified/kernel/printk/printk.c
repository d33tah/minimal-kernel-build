
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/console.h>




/* The printk ring buffer (printk_ringbuffer.h structs/macros) was removed:
 * CONFIG_PRINTK is unset, so prb_read_valid()/prb_next_seq()/prb_first_valid_seq()
 * are constant stubs, no struct printk_ringbuffer is ever instantiated, and the
 * record-emit path (console_emit_next_record) never reads a record. */
#include "internal.h"

int console_printk[4] = {
	CONSOLE_LOGLEVEL_DEFAULT,	 
	MESSAGE_LOGLEVEL_DEFAULT,	 
	CONSOLE_LOGLEVEL_MIN,		 
	CONSOLE_LOGLEVEL_DEFAULT,	 
};

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




void console_verbose(void)
{
	if (console_loglevel)
		console_loglevel = CONSOLE_LOGLEVEL_MOTORMOUTH;
}

void console_lock(void)
{
	might_sleep();

	down_console_sem();
}

static void __console_unlock(void)
{
	up_console_sem();
}

void console_unlock(void)
{
	/*
	 * On this !CONFIG_PRINTK build there is no ring buffer, so
	 * console_emit_next_record() never reads a record and the flush
	 * loop has nothing to do. Just release the console lock.
	 */
	__console_unlock();
}


void console_unblank(void)
{
	struct console *c;

	 
	if (oops_in_progress) {
		if (down_trylock_console_sem() != 0)
			return;
	} else
		console_lock();

	for_each_console(c)
		if ((c->flags & CON_ENABLED) && c->unblank)
			c->unblank();
	console_unlock();
}

void console_flush_on_panic(enum con_flush_mode mode)
{
	/*
	 * The only caller passes CONSOLE_FLUSH_PENDING, and with no ring
	 * buffer there is nothing to flush — just take and drop the lock.
	 */
	down_trylock_console_sem();
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


static int try_enable_preferred_console(struct console *newcon)
{
	/*
	 * console_cmdline[] is never populated on this build (no console=
	 * boot-param parser / __add_preferred_console), so the cmdline-match
	 * loop never iterates. The only live register_console pass is the
	 * unspecified one (user_specified == false), where the post-loop check
	 * folds to plain CON_ENABLED; the user_specified == true pass always
	 * returned -ENOENT and was statically dead, so it was folded out.
	 */
	if (newcon->flags & CON_ENABLED)
		return 0;

	return -ENOENT;
}

static void try_enable_default_console(struct console *newcon)
{
	if (newcon->index < 0)
		newcon->index = 0;

	/* No console on this build sets a ->setup callback, so it is always
	 * NULL here; the setup-failure early-return is statically dead. */

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
	int err;

	for_each_console(con) {
		if (WARN(con == newcon, "console '%s%d' already registered\n",
					 con->name, con->index))
			return;
	}

	/*
	 * No console on this build sets CON_BOOT (the only registrant is
	 * vt_console_driver, CON_PRINTBUFFER) so there is never a boot console
	 * to track or unregister; the bootcon machinery below is statically
	 * dead and has been folded out.
	 *
	 * preferred_console is always -1 here (no console= cmdline param).
	 */
	if (!console_drivers || !console_drivers->device)
		try_enable_default_console(newcon);


	err = try_enable_preferred_console(newcon);


	if (err || newcon->flags & CON_BRL)
		return;


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

	console_unlock();

	con_printk(KERN_INFO, newcon, "enabled\n");
}

void __init console_init(void)
{
	initcall_t call;
	initcall_entry_t *ce;

	 
	n_tty_init();

	 
	ce = __con_initcall_start;
	 
	while (ce < __con_initcall_end) {
		call = initcall_from_entry(ce);

		call();
		 
		ce++;
	}
}



