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
#define VGA_PEL_MSK	0x3C6
#define VGA_CRT_IC	0x3D4
#define VGA_CRT_IM	0x3B4
#define VGA_PEL_IW	0x3C8

/* VGA CRTC registers - only those used */
#define VGA_CRTC_H_DISP		1
#define VGA_CRTC_OVERFLOW	7
#define VGA_CRTC_MAX_SCAN	9
#define VGA_CRTC_CURSOR_START	0x0A
#define VGA_CRTC_CURSOR_END	0x0B
#define VGA_CRTC_V_SYNC_END	0x11
#define VGA_CRTC_V_DISP_END	0x12
#define VGA_CRTC_OFFSET		0x13
#define VGA_CRTC_MODE		0x17

struct vgastate {
	void __iomem *vgabase;
	unsigned long membase;
	__u32 memsize;
	__u32 flags;
	__u32 depth;
	__u32 num_attr;
	__u32 num_crtc;
	__u32 num_gfx;
	__u32 num_seq;
	void *vidstate;
};

/* Only vga_w is used in vgacon.c */
static inline void vga_io_w(unsigned short port, unsigned char val)
{
	outb_p(val, port);
}

static inline void vga_mm_w(void __iomem *regbase, unsigned short port, unsigned char val)
{
	writeb(val, regbase + port);
}

static inline void vga_w(void __iomem *regbase, unsigned short port, unsigned char val)
{
	if (regbase)
		vga_mm_w(regbase, port, val);
	else
		vga_io_w(port, val);
}

#endif  
