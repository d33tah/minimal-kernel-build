/* Minimal includes for debug_locks */
#include <linux/debug_locks.h>
#include <linux/printk.h>

int debug_locks __read_mostly = 1;

int debug_locks_silent __read_mostly;

int debug_locks_off(void)
{
	if (debug_locks && __debug_locks_off()) {
		if (!debug_locks_silent) {
			console_verbose();
			return 1;
		}
	}
	return 0;
}
