
#include <linux/module.h>
#include <linux/export.h>

/* register_keyboard_notifier, unregister_keyboard_notifier, kd_mksound,
   vt_get_leds removed - unused */

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


void vt_reset_keyboard(int console)
{
	 
}

void vt_set_leds_compute_shiftstate(void)
{

}

/* vt_do_diacrit, vt_do_kdskbmode, vt_do_kdskbmeta, vt_do_kbkeycode_ioctl,
   vt_do_kdsk_ioctl, vt_do_kdgkb_ioctl, vt_do_kdskled, vt_do_kdgkbmode,
   vt_do_kdgkbmeta removed - unused (keyboard ioctls not needed) */

int vt_reset_unicode(int console)
{
	return 0;
}

int __init kbd_init(void)
{
	return 0;
}
