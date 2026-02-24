
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/console.h>

/* end printk_ringbuffer.h */
#define printk_safe_enter_irqsave(flags) local_irq_save(flags)
#define printk_safe_exit_irqrestore(flags) local_irq_restore(flags)
/* end internal.h */

atomic_t ignore_console_lock_warning __read_mostly = ATOMIC_INIT(0);

int oops_in_progress;

static DEFINE_SEMAPHORE(console_sem);
struct console *console_drivers;

#define down_console_sem() down(&console_sem)

static int down_trylock_console_sem(void)
{
	int lock_failed;
	unsigned long flags;

	printk_safe_enter_irqsave(flags);
	lock_failed = down_trylock(&console_sem);
	printk_safe_exit_irqrestore(flags);

	if (lock_failed)
		return 1;
	return 0;
}

static int console_locked;

void console_verbose(void)
{
}

void console_lock(void)
{
	down_console_sem();
	console_locked = 1;
}

int is_console_locked(void)
{
	return console_locked;
}

void console_unlock(void)
{
	unsigned long flags;
	console_locked = 0;
	printk_safe_enter_irqsave(flags);
	up(&console_sem);
	printk_safe_exit_irqrestore(flags);
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
	for_each_console(c)
		if ((c->flags & CON_ENABLED) && c->unblank)
			c->unblank();
	console_unlock();
}

void console_flush_on_panic(enum con_flush_mode mode)
{
}

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

	if (!console_drivers || !console_drivers->device ||
	    console_drivers->flags & CON_BOOT) {
		if (newcon->index < 0)
			newcon->index = 0;
		if (!newcon->setup || newcon->setup(newcon, NULL) == 0) {
			newcon->flags |= CON_ENABLED;
			if (newcon->device)
				newcon->flags |= CON_CONSDEV;
		}
	}

	err = (newcon->flags & CON_ENABLED) ? 0 : -ENOENT;

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

	console_unlock();
	con_printk(KERN_INFO, newcon, "enabled\n");
}

void __init console_init(void)
{
	initcall_t call;
	initcall_entry_t *ce;

	ce = __con_initcall_start;

	while (ce < __con_initcall_end) {
		call = initcall_from_entry(ce);

		call();

		ce++;
	}
}
