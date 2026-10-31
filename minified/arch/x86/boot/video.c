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
}

static void store_video_mode(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	ireg.ah = 0x0f;
	intcall(0x10, &ireg, &oreg);

	boot_params.screen_info.orig_video_mode = oreg.al & 0x7f;
}

static void store_mode_params(void)
{
	int x, y;

	store_cursor_position();
	store_video_mode();

	set_fs(0);
	x = rdfs16(0x44a);
	y = (adapter == ADAPTER_CGA) ? 25 : rdfs8(0x484) + 1;

	boot_params.screen_info.orig_video_cols = x;
	boot_params.screen_info.orig_video_lines = y;
}

/*
 * vgacon reads orig_video_mode/isVGA/cols/lines/orig_x/orig_y; the CGA
 * check decides how many lines there are. No mode switch.
 */
static void detect_adapter(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	ireg.ax = 0x1200;
	ireg.bl = 0x10;
	intcall(0x10, &ireg, &oreg);

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
