
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/console.h>

/* end printk_ringbuffer.h */
#define printk_safe_enter_irqsave(flags) local_irq_save(flags)
#define printk_safe_exit_irqrestore(flags) local_irq_restore(flags)
/* end internal.h */

int oops_in_progress;

static DEFINE_SEMAPHORE(console_sem);
struct console *console_drivers;

static int console_locked;

void console_lock(void)
{
	down(&console_sem);
	console_locked = 1;
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
}

void register_console(struct console *newcon)
{
	if (newcon->index < 0)
		newcon->index = 0;
	newcon->flags |= CON_ENABLED;

	console_lock();
	newcon->next = console_drivers;
	console_drivers = newcon;
	console_unlock();
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
