#include <linux/tty.h>
#include <linux/console.h>

/*
 * printk() is a no-op in this kernel (no log buffer), so nothing is ever
 * printed through a struct console. What remains is the console list that
 * /dev/console resolves through (console_device) and the console lock that
 * serialises the VT.
 */
static DEFINE_SEMAPHORE(console_sem);
static struct console *console_drivers;

void console_lock(void)
{
	might_sleep();
	down(&console_sem);
}

void console_unlock(void)
{
	up(&console_sem);
}

struct tty_driver *console_device(int *index)
{
	struct console *c;
	struct tty_driver *driver = NULL;

	console_lock();
	for (c = console_drivers; c && !driver; c = c->next)
		driver = c->device(c, index);
	console_unlock();
	return driver;
}

void register_console(struct console *newcon)
{
	struct console **p;

	console_lock();
	for (p = &console_drivers; *p; p = &(*p)->next)
		;
	newcon->next = NULL;
	*p = newcon;
	console_unlock();
}

void __init console_init(void)
{
	initcall_entry_t *ce;

	for (ce = __con_initcall_start; ce < __con_initcall_end; ce++)
		initcall_from_entry(ce)();
}
