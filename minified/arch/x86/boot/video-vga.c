/* Minimal VGA driver - only basic 80x25 text mode needed */
#include "boot.h"
#include "video.h"

/* Only support 80x25 mode for minimal kernel */
static struct mode_info vga_modes[] = {
	{ VIDEO_80x25, 80, 25, 0 },
};

static __videocard video_vga;

static u8 vga_set_basic_mode(void)
{
	struct biosregs ireg, oreg;
	u8 mode;

	initregs(&ireg);
	ireg.ax = 0x0f00;
	intcall(0x10, &ireg, &oreg);
	mode = oreg.al;

	if (mode != 3 && mode != 7)
		mode = 3;

	ireg.ax = mode;
	intcall(0x10, &ireg, NULL);
	do_restore = 1;
	return mode;
}

u16 vga_crtc(void)
{
	return (inb(0x3cc) & 1) ? 0x3d4 : 0x3b4;
}

static int vga_set_mode(struct mode_info *mode)
{
	vga_set_basic_mode();
	force_x = mode->x;
	force_y = mode->y;
	return 0;
}

static int vga_probe(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	ireg.ax = 0x1200;
	ireg.bl = 0x10;
	intcall(0x10, &ireg, &oreg);

#ifndef _WAKEUP
	boot_params.screen_info.orig_video_ega_bx = oreg.bx;
#endif

	if (oreg.bl != 0x10) {
		ireg.ax = 0x1a00;
		intcall(0x10, &ireg, &oreg);

		if (oreg.al == 0x1a) {
			adapter = ADAPTER_VGA;
#ifndef _WAKEUP
			boot_params.screen_info.orig_video_isVGA = 1;
#endif
		} else {
			adapter = ADAPTER_EGA;
		}
	} else {
		adapter = ADAPTER_CGA;
	}

	video_vga.modes = vga_modes;
	video_vga.card_name = "VGA";
	return 1;  /* Always return 1 mode (80x25) */
}

static __videocard video_vga = {
	.card_name	= "VGA",
	.probe		= vga_probe,
	.set_mode	= vga_set_mode,
};
