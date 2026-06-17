/* Minimal includes for tty jobctrl stubs */
#include <linux/tty.h>
#include <linux/sched.h>
#include "tty.h"

struct tty_struct *get_current_tty(void)
{
	return NULL;
}
