#include "boot.h"
#include "video.h"

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

void set_video(void)
{
	vga_probe();
	store_mode_params();
}
