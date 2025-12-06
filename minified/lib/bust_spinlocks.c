/* Minimal includes for bust_spinlocks */
#include <linux/printk.h>
#include <linux/vt_kern.h>
#include <linux/console.h>

void bust_spinlocks(int yes)
{
	if (yes) {
		++oops_in_progress;
	} else {
		unblank_screen();
		console_unblank();
		if (--oops_in_progress == 0)
			wake_up_klogd();
	}
}
