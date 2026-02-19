#include <uapi/asm/boot.h>
#include "boot.h"
#include "video.h"

static void store_mode_params(void)
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

	initregs(&ireg);
	ireg.ah = 0x0f;
	intcall(0x10, &ireg, &oreg);
	boot_params.screen_info.orig_video_mode = oreg.al & 0x7f;
	boot_params.screen_info.orig_video_page = oreg.bh;

	set_fs(0);
	boot_params.screen_info.orig_video_points = rdfs16(0x485);
	boot_params.screen_info.orig_video_cols = rdfs16(0x44a);
	boot_params.screen_info.orig_video_lines = rdfs8(0x484) + 1;
	boot_params.screen_info.orig_video_isVGA = 1;
}

void set_video(void)
{
	RESET_HEAP();
	store_mode_params();
	boot_params.hdr.vid_mode = boot_params.screen_info.orig_video_mode;
}
