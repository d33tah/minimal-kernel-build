/* Minimal VGA header - only what vgacon.c needs */
#ifndef __linux_video_vga_h__
#define __linux_video_vga_h__

#include <linux/types.h>
#include <linux/io.h>
#include <asm/vga.h>

/* VGA port addresses - only those used */
#define VGA_CRT_DC	0x3D5
#define VGA_CRT_DM	0x3B5
#define VGA_ATT_W	0x3C0
#define VGA_IS1_RC	0x3DA
#define VGA_PEL_D	0x3C9
#define VGA_CRT_IC	0x3D4
#define VGA_CRT_IM	0x3B4
#define VGA_PEL_IW	0x3C8

/* VGA CRTC registers - only those used */
#define VGA_CRTC_CURSOR_START	0x0A
#define VGA_CRTC_CURSOR_END	0x0B

struct vgastate {
	void __iomem *vgabase;
};

/* vga_io_w, vga_mm_w, vga_w removed - unused */

#endif
