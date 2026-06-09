
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
#include <linux/selection.h>


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
	/* Stub: SAK (Secure Attention Key) never called in minimal kernel */
}

