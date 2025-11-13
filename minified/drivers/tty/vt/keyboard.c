// SPDX-License-Identifier: GPL-2.0
/*
 * Stubbed keyboard driver - no keyboard input needed for minimal kernel
 */

#include <linux/module.h>
#include <linux/export.h>
#include <linux/notifier.h>

/* Stub: register_keyboard_notifier */
int register_keyboard_notifier(struct notifier_block *nb)
{
	return -ENODEV;
}

/* Stub: unregister_keyboard_notifier */
int unregister_keyboard_notifier(struct notifier_block *nb)
{
	return -ENODEV;
}

/* Stub: kd_mksound */
void kd_mksound(unsigned int hz, unsigned int ticks)
{
	/* No-op: no sound support */
}

/* Stub: vt_get_leds */
int vt_get_leds(int console, int flag)
{
	return 0;
}

/* Stub: vt_kbd_con_start */
void vt_kbd_con_start(int console)
{
	/* No-op */
}

/* Stub: vt_kbd_con_stop */
void vt_kbd_con_stop(int console)
{
	/* No-op */
}

/* Stub: vt_get_kbd_mode_bit */
int vt_get_kbd_mode_bit(int console, int bit)
{
	return 0;
}

/* Stub: vt_set_kbd_mode_bit */
void vt_set_kbd_mode_bit(int console, int bit)
{
	/* No-op */
}

/* Stub: vt_clr_kbd_mode_bit */
void vt_clr_kbd_mode_bit(int console, int bit)
{
	/* No-op */
}

/* Stub: vt_set_led_state */
void vt_set_led_state(int console, int leds)
{
	/* No-op */
}

/* Stub: vt_get_shift_state */
int vt_get_shift_state(void)
{
	return 0;
}

/* Stub: vt_reset_keyboard */
void vt_reset_keyboard(int console)
{
	/* No-op */
}

/* Stub: vt_set_leds_compute_shiftstate */
void vt_set_leds_compute_shiftstate(void)
{
	/* No-op */
}

/* Stub: vt_do_diacrit */
int vt_do_diacrit(unsigned int cmd, void __user *udp, int perm)
{
	return -EINVAL;
}

/* Stub: vt_do_kdskbmode */
int vt_do_kdskbmode(int console, unsigned int arg)
{
	return -EINVAL;
}

/* Stub: vt_do_kdskbmeta */
int vt_do_kdskbmeta(int console, unsigned int arg)
{
	return -EINVAL;
}

/* Stub: vt_do_kbkeycode_ioctl */
int vt_do_kbkeycode_ioctl(int cmd, struct kbkeycode __user *user_kbkc, int perm)
{
	return -EINVAL;
}

/* Stub: vt_do_kdsk_ioctl */
int vt_do_kdsk_ioctl(int cmd, struct kbentry __user *user_kbe, int perm, int console)
{
	return -EINVAL;
}

/* Stub: vt_do_kdgkb_ioctl */
int vt_do_kdgkb_ioctl(int cmd, struct kbsentry __user *user_kdgkb, int perm)
{
	return -EINVAL;
}

/* Stub: vt_do_kdskled */
int vt_do_kdskled(int console, int cmd, unsigned long arg, int perm)
{
	return -EINVAL;
}

/* Stub: vt_do_kdgkbmode */
int vt_do_kdgkbmode(int console)
{
	return 0;
}

/* Stub: vt_do_kdgkbmeta */
int vt_do_kdgkbmeta(int console)
{
	return 0;
}

/* Stub: vt_spawn_con */
int vt_spawn_con(unsigned int vt_num)
{
	return -EINVAL;
}

/* Stub: vt_reset_unicode */
int vt_reset_unicode(int console)
{
	return 0;
}

/* Stub: vt_get_kb */
int vt_get_kb(unsigned int console)
{
	return 0;
}

/* Stub: vt_set_kb */
int vt_set_kb(unsigned int console, unsigned int kb)
{
	return 0;
}

/* Stub: kbd_init */
int __init kbd_init(void)
{
	return 0;
}

/* Stub: kbd_rate */
int kbd_rate(struct kbd_repeat *rpt)
{
	return -EINVAL;
}
