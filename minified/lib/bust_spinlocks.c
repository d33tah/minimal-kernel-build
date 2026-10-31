/* Minimal includes for bust_spinlocks */
#include <linux/printk.h>
#include <linux/console.h>

void bust_spinlocks(int yes)
{
	if (yes) {
		++oops_in_progress;
	} else {
		console_unblank();
		--oops_in_progress;
	}
}
