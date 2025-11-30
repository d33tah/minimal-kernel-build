 
 

#include <linux/module.h>
#include <linux/export.h>
#include <linux/notifier.h>

 
int register_keyboard_notifier(struct notifier_block *nb)
{
	return -ENODEV;
}

 
int unregister_keyboard_notifier(struct notifier_block *nb)
{
	return -ENODEV;
}

 
void kd_mksound(unsigned int hz, unsigned int ticks)
{
	 
}

 
int vt_get_leds(int console, int flag)
{
	return 0;
}

 
void vt_kbd_con_start(int console)
{
	 
}

 
void vt_kbd_con_stop(int console)
{
	 
}

 
int vt_get_kbd_mode_bit(int console, int bit)
{
	return 0;
}

 
void vt_set_kbd_mode_bit(int console, int bit)
{
	 
}

 
void vt_clr_kbd_mode_bit(int console, int bit)
{
	 
}

 
void vt_set_led_state(int console, int leds)
{
	 
}

 
int vt_get_shift_state(void)
{
	return 0;
}

 
void vt_reset_keyboard(int console)
{
	 
}

 
void vt_set_leds_compute_shiftstate(void)
{
	 
}

 
int vt_do_diacrit(unsigned int cmd, void __user *udp, int perm)
{
	return -EINVAL;
}

 
int vt_do_kdskbmode(int console, unsigned int arg)
{
	return -EINVAL;
}

 
int vt_do_kdskbmeta(int console, unsigned int arg)
{
	return -EINVAL;
}

 
int vt_do_kbkeycode_ioctl(int cmd, struct kbkeycode __user *user_kbkc, int perm)
{
	return -EINVAL;
}

 
int vt_do_kdsk_ioctl(int cmd, struct kbentry __user *user_kbe, int perm, int console)
{
	return -EINVAL;
}

 
int vt_do_kdgkb_ioctl(int cmd, struct kbsentry __user *user_kdgkb, int perm)
{
	return -EINVAL;
}

 
int vt_do_kdskled(int console, int cmd, unsigned long arg, int perm)
{
	return -EINVAL;
}

 
int vt_do_kdgkbmode(int console)
{
	return 0;
}

 
int vt_do_kdgkbmeta(int console)
{
	return 0;
}

 
int vt_spawn_con(unsigned int vt_num)
{
	return -EINVAL;
}

 
int vt_reset_unicode(int console)
{
	return 0;
}

 
int vt_get_kb(unsigned int console)
{
	return 0;
}

 
int vt_set_kb(unsigned int console, unsigned int kb)
{
	return 0;
}

 
int __init kbd_init(void)
{
	return 0;
}

 
int kbd_rate(struct kbd_repeat *rpt)
{
	return -EINVAL;
}
