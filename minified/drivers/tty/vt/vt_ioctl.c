/* Minimal includes for vt_ioctl stubs */
#include <linux/kd.h>
#include <linux/vt_kern.h>
#include <linux/workqueue.h>

void reset_vc(struct vc_data *vc)
{
	vc->vc_mode = KD_TEXT;
	/* vt_reset_unicode, vt_mode, vt_pid, vt_newvt removed - empty stubs */
	reset_palette(vc);
}

void vc_SAK(struct work_struct *work)
{
	/* Stub: SAK (Secure Attention Key) never called in minimal kernel */
}
