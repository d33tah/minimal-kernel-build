/* Minimal includes for tty jobctrl stubs */
#include <linux/tty.h>
#include <linux/sched.h>
#include "tty.h"

void tty_open_proc_set_tty(struct file *filp, struct tty_struct *tty)
{
}

struct tty_struct *get_current_tty(void)
{
	return NULL;
}

void disassociate_ctty(int on_exit)
{
}
