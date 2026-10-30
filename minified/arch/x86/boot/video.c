#include "boot.h"

#define ADAPTER_CGA 0
#define ADAPTER_EGA 1
#define ADAPTER_VGA 2

static int adapter;

static void store_cursor_position(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	ireg.ah = 0x03;
	intcall(0x10, &ireg, &oreg);

	boot_params.screen_info.orig_x = oreg.dl;
	boot_params.screen_info.orig_y = oreg.dh;

	if (oreg.ch & 0x20)
		boot_params.screen_info.flags |= VIDEO_FLAGS_NOCURSOR;

	if ((oreg.ch & 0x1f) > (oreg.cl & 0x1f))
		boot_params.screen_info.flags |= VIDEO_FLAGS_NOCURSOR;
}

static void store_video_mode(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	ireg.ah = 0x0f;
	intcall(0x10, &ireg, &oreg);

	boot_params.screen_info.orig_video_mode = oreg.al & 0x7f;
	boot_params.screen_info.orig_video_page = oreg.bh;
}

static void store_mode_params(void)
{
	u16 font_size;
	int x, y;

	store_cursor_position();
	store_video_mode();

	set_fs(0);
	font_size = rdfs16(0x485);
	boot_params.screen_info.orig_video_points = font_size;

	x = rdfs16(0x44a);
	y = (adapter == ADAPTER_CGA) ? 25 : rdfs8(0x484) + 1;

	boot_params.screen_info.orig_video_cols = x;
	boot_params.screen_info.orig_video_lines = y;
}

/* Fills orig_video_ega_bx/orig_video_isVGA for vgacon; no mode switch. */
static void detect_adapter(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	ireg.ax = 0x1200;
	ireg.bl = 0x10;
	intcall(0x10, &ireg, &oreg);

	boot_params.screen_info.orig_video_ega_bx = oreg.bx;

	if (oreg.bl != 0x10) {
		ireg.ax = 0x1a00;
		intcall(0x10, &ireg, &oreg);

		if (oreg.al == 0x1a) {
			adapter = ADAPTER_VGA;
			boot_params.screen_info.orig_video_isVGA = 1;
		} else {
			adapter = ADAPTER_EGA;
		}
	} else {
		adapter = ADAPTER_CGA;
	}
}

void set_video(void)
{
	detect_adapter();
	store_mode_params();
}
