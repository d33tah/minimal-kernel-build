/* Minimal includes for tty jobctrl stubs */
#include <linux/tty.h>
#include <linux/sched.h>
#include "tty.h"

int __tty_check_change(struct tty_struct *tty, int sig)
{
	return 0;
}

int tty_check_change(struct tty_struct *tty)
{
	return 0;
}

void proc_clear_tty(struct task_struct *p)
{
}

void tty_open_proc_set_tty(struct file *filp, struct tty_struct *tty)
{
}

struct tty_struct *get_current_tty(void)
{
	return NULL;
}

void session_clear_tty(struct pid *session)
{
}

int tty_signal_session_leader(struct tty_struct *tty, int exit_session)
{
	return 0;
}

void disassociate_ctty(int on_exit)
{
}

void no_tty(void)
{
}

struct pid *tty_get_pgrp(struct tty_struct *tty)
{
	return NULL;
}

long tty_jobctrl_ioctl(struct tty_struct *tty, struct tty_struct *real_tty,
		       struct file *file, unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}
