/* --- 2026-02-06 22:20 --- Gutted: entire TTY open/read/write/close path
 * is dead because console_driver is never initialized (vty_init removed).
 * Console output goes through vt_console_print directly.
 * Only tty_kref_put and tty_class remain as externally-referenced symbols. */

#include <linux/types.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/mutex.h>

LIST_HEAD(tty_drivers);
DEFINE_MUTEX(tty_mutex);
struct class *tty_class;

void tty_kref_put(struct tty_struct *tty)
{
	/* sig->tty is always NULL in this minimal kernel,
	   so this is effectively a no-op. Keep for exit.c. */
	if (tty)
		kref_put(&tty->kref, NULL);
}
