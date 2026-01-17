/* Minimal includes for bust_spinlocks */
#include <linux/printk.h>
#include <linux/console.h>
void bust_spinlocks(int yes)
{
	if (yes) {
		++oops_in_progress;
	} else {
		/* unblank_screen removed - was empty stub */
		console_unblank();
		--oops_in_progress;
		/* wake_up_klogd removed - empty stub */
	}
}
