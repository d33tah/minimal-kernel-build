/* Minimal video setup for boot - menu and restore removed */
#include <uapi/asm/boot.h>
#include "boot.h"
#include "video.h"
#include "vesa.h"

static u16 video_segment;

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

	if (graphic_mode)
		return;

	store_cursor_position();
	store_video_mode();

	if (boot_params.screen_info.orig_video_mode == 0x07) {
		video_segment = 0xb000;
	} else {
		video_segment = 0xb800;
	}

	set_fs(0);
	font_size = rdfs16(0x485);
	boot_params.screen_info.orig_video_points = font_size;

	x = rdfs16(0x44a);
	y = (adapter == ADAPTER_CGA) ? 25 : rdfs8(0x484)+1;

	if (force_x)
		x = force_x;
	if (force_y)
		y = force_y;

	boot_params.screen_info.orig_video_cols  = x;
	boot_params.screen_info.orig_video_lines = y;
}

void set_video(void)
{
	u16 mode = boot_params.hdr.vid_mode;

	RESET_HEAP();
	store_mode_params();
	probe_cards(0);

	/* For minimal kernel, use VIDEO_CURRENT_MODE or NORMAL_VGA */
	if (mode == ASK_VGA)
		mode = VIDEO_CURRENT_MODE;

	if (set_mode(mode)) {
		/* Fall back to current mode if requested mode fails */
		set_mode(VIDEO_CURRENT_MODE);
	}

	boot_params.hdr.vid_mode = mode;
	vesa_store_edid();
	store_mode_params();
}
