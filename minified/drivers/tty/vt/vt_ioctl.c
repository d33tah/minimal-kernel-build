
#include <linux/types.h>
#include <linux/sched/signal.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <linux/compat.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/fs.h>
#include <linux/console.h>
#include <linux/consolemap.h>
#include <linux/timex.h>

#include <asm/io.h>

#include <linux/nospec.h>

#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>


void reset_vc(struct vc_data *vc)
{
	vc->vc_mode = KD_TEXT;
	vt_reset_unicode(vc->vc_num);
	reset_palette(vc);
}

