
#include <linux/types.h>
#include <linux/tty.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/consolemap.h>
#include <linux/vt_kern.h>



#include <linux/kbd_kern.h>
#include <linux/selection.h>


void reset_vc(struct vc_data *vc)
{
	vc->vc_mode = KD_TEXT;
	vt_reset_unicode(vc->vc_num);
	reset_palette(vc);
}

