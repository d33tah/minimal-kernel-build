
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched/signal.h>
#include <linux/tty.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/compat.h>
#include <linux/module.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/console.h>
#include <linux/consolemap.h>
#include <linux/signal.h>
#include <linux/suspend.h>
#include <linux/timex.h>

#include <asm/io.h>
#include <linux/uaccess.h>

#include <linux/nospec.h>

#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
/* kbd_diacr.h inlined */
extern struct kbdiacruc accent_table[];
extern unsigned int accent_table_size;
#include <linux/selection.h>

bool vt_dont_switch;

void vt_event_post(unsigned int event, unsigned int old, unsigned int new)
{
}

int vt_waitactive(int n)
{
	return 0;
}

int vt_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg)
{
	struct vc_data *vc = tty->driver_data;
	void __user *up = (void __user *)arg;

	switch (cmd) {
	case KDGETMODE:
		return put_user(vc->vc_mode, (int __user *)arg);
	case KDGKBTYPE:
		return put_user(KB_101, (char __user *)arg);
	case VT_GETSTATE:
	{
		struct vt_stat __user *vtstat = up;
		if (put_user(fg_console + 1, &vtstat->v_active))
			return -EFAULT;
		return put_user(1, &vtstat->v_state);
	}
	case VT_GETHIFONTMASK:
		return put_user(vc->vc_hi_font_mask, (unsigned short __user *)arg);
	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

void reset_vc(struct vc_data *vc)
{
	vc->vc_mode = KD_TEXT;
	vt_reset_unicode(vc->vc_num);
	vc->vt_mode.mode = VT_AUTO;
	vc->vt_mode.waitv = 0;
	vc->vt_mode.relsig = 0;
	vc->vt_mode.acqsig = 0;
	vc->vt_mode.frsig = 0;
	put_pid(vc->vt_pid);
	vc->vt_pid = NULL;
	vc->vt_newvt = -1;
	reset_palette(vc);
}

void vc_SAK(struct work_struct *work)
{
	struct vc *vc_con = container_of(work, struct vc, SAK_work);
	struct vc_data *vc;
	struct tty_struct *tty;

	console_lock();
	vc = vc_con->d;
	if (vc) {
		tty = vc->port.tty;
		if (tty)
			__do_SAK(tty);
		reset_vc(vc);
	}
	console_unlock();
}

void change_console(struct vc_data *new_vc)
{
}

int vt_move_to_console(unsigned int vt, int alloc)
{
	return 0;
}

void pm_set_vt_switch(int do_switch)
{
}
