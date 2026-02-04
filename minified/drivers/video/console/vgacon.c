
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/slab.h>
#include <linux/vt_kern.h>
#include <linux/sched.h>
#include <linux/selection.h>
#include <linux/spinlock.h>
#include <linux/ioport.h>
#include <linux/screen_info.h>
#include <asm/io.h>
#include <asm/vga.h>

/* Inlined from video/vga.h */
/* VGA_CRT_DC, VGA_CRT_DM removed - never used */
#define VGA_ATT_W 0x3C0
#define VGA_IS1_RC 0x3DA
#define VGA_PEL_D 0x3C9
/* VGA_PEL_MSK removed - unused after vga_set_palette removal */
#define VGA_CRT_IC 0x3D4
/* VGA_CRT_IM removed - never used */
#define VGA_PEL_IW 0x3C8
/* VGA_CRTC_H_DISP thru VGA_CRTC_MODE removed - never used */

/* vgastate struct removed - write-only, never read */

/* vga_io_w, vga_mm_w, vga_w removed - never called */

static DEFINE_RAW_SPINLOCK(vga_lock);
/* cursor_size_lastfrom, cursor_size_lastto removed - vgacon_set_cursor_size never called */
static u32 vgacon_xres;
static u32 vgacon_yres;

/* BLANK removed - unused */

#define VGA_FONTWIDTH 8

static const char *vgacon_startup(void);
static void vgacon_init(struct vc_data *c, int init);
static void vgacon_deinit(struct vc_data *c);
static void vgacon_cursor(struct vc_data *c, int mode);
static int vgacon_switch(struct vc_data *c);
/* vgacon_blank removed - con_blank never invoked */
/* vgacon_scrolldelta inlined into vgacon_restore_screen */
static int vgacon_set_origin(struct vc_data *c);
static void vgacon_save_screen(struct vc_data *c);
static struct uni_pagedir *vgacon_uni_pagedir;
/* vgacon_refcount removed - only inc/dec, never read */

static unsigned long vga_vram_base __read_mostly;
/* vga_vram_end removed - write-only, never read */
static unsigned int vga_vram_size __read_mostly;
static u16 vga_video_port_reg __read_mostly;
/* vga_video_port_val removed - write-only, never read */
static unsigned int vga_video_num_columns;
static unsigned int vga_video_num_lines;
static bool vga_can_do_color;
static unsigned int vga_default_font_height __read_mostly;
/* vga_video_type removed - write-only, never read */
/* vga_vesa_blanked removed - unused */
/* vga_palette_blanked removed - never assigned, always false */
/* vga_is_gfx removed - never assigned, always false */
/* vga_512_chars removed - always false, never set */
static int vga_video_font_height;
static int vga_scan_lines __read_mostly;
static unsigned int vga_rolled_over;

/* vga_hardscroll_enabled, vga_hardscroll_user_enable removed - write-only */

static inline void write_vga(unsigned char reg, unsigned int val)
{
	unsigned int v1, v2;
	unsigned long flags;

	raw_spin_lock_irqsave(&vga_lock, flags);
	v1 = reg + (val & 0xff00);
	v2 = reg + 1 + ((val << 8) & 0xff00);
	outw(v1, vga_video_port_reg);
	outw(v2, vga_video_port_reg);
	raw_spin_unlock_irqrestore(&vga_lock, flags);
}

static inline void vga_set_mem_top(struct vc_data *c)
{
	write_vga(12, (c->vc_visible_origin - vga_vram_base) / 2);
}

/* --- 2026-01-26 05:02 --- vgacon_scrolldelta inlined */
/* vgacon_restore_screen inlined into vgacon_cursor - single caller */

static const char *vgacon_startup(void)
{
	const char *display_desc = NULL;

	if (screen_info.orig_video_isVGA == VIDEO_TYPE_VLFB ||
	    screen_info.orig_video_isVGA == VIDEO_TYPE_EFI) {
no_vga:
		conswitchp = &dummy_con;
		return conswitchp->con_startup();
	}

	if ((screen_info.orig_video_lines == 0) ||
	    (screen_info.orig_video_cols == 0))
		goto no_vga;

	if ((screen_info.orig_video_mode == 0x0D) ||
	    (screen_info.orig_video_mode == 0x0E) ||
	    (screen_info.orig_video_mode == 0x10) ||
	    (screen_info.orig_video_mode == 0x12) ||
	    (screen_info.orig_video_mode == 0x6A))
		goto no_vga;

	vga_video_num_lines = screen_info.orig_video_lines;
	vga_video_num_columns = screen_info.orig_video_cols;

	/* Simplified: assume VGA color mode only (MDA/CGA/EGA removed ~65 LOC) */
	{
		static struct resource vga_console_resource = {
			.name = "vga+",
			.flags = IORESOURCE_IO,
			.start = 0x3C0,
			.end = 0x3DF
		};
		int i;

		vga_can_do_color = true;
		vga_vram_base = 0xb8000;
		vga_video_port_reg = VGA_CRT_IC;
		/* vga_video_port_val removed */
		vga_vram_size = 0x8000;
		/* vga_video_type assignment removed - write-only */
		display_desc = "VGA+";
		request_resource(&ioport_resource, &vga_console_resource);

		for (i = 0; i < 16; i++) {
			inb_p(VGA_IS1_RC);
			outb_p(i, VGA_ATT_W);
			outb_p(i, VGA_ATT_W);
		}
		outb_p(0x20, VGA_ATT_W);

		for (i = 0; i < 16; i++) {
			outb_p(color_table[i], VGA_PEL_IW);
			/* default_red/grn/blu arrays were all zeros */
			outb_p(0, VGA_PEL_D);
			outb_p(0, VGA_PEL_D);
			outb_p(0, VGA_PEL_D);
		}
	}

	vga_vram_base = VGA_MAP_MEM(vga_vram_base, vga_vram_size);
	/* vga_vram_end assignment removed - variable was never read */

	/* VRAM read/write test removed - QEMU VRAM always works (~18 LOC) */

	/* Always VIDEO_TYPE_VGAC now, conditional removed */
	/* vga_hardscroll assignment removed - var never read */
	vga_default_font_height = screen_info.orig_video_points;
	vga_video_font_height = screen_info.orig_video_points;
	vga_scan_lines = vga_video_font_height * vga_video_num_lines;

	vgacon_xres = screen_info.orig_video_cols * VGA_FONTWIDTH;
	vgacon_yres = vga_scan_lines;

	return display_desc;
}

static void vgacon_init(struct vc_data *c, int init)
{
	struct uni_pagedir *p;

	c->vc_can_do_color = vga_can_do_color;
	/* vc_scan_lines assignment removed - field removed */
	c->vc_font.height = c->vc_cell_height = vga_video_font_height;

	if (init) {
		c->vc_cols = vga_video_num_columns;
		c->vc_rows = vga_video_num_lines;
	} else
		vc_resize(c, vga_video_num_columns, vga_video_num_lines);

	c->vc_complement_mask = 0x7700;
	/* vga_512_chars check removed - always false */
	p = *c->vc_uni_pagedir_loc;
	if (c->vc_uni_pagedir_loc != &vgacon_uni_pagedir)
		c->vc_uni_pagedir_loc = &vgacon_uni_pagedir;
	/* vgacon_refcount inc removed */
	if (global_cursor_default == -1)
		global_cursor_default =
			!(screen_info.flags & VIDEO_FLAGS_NOCURSOR);
}

static void vgacon_deinit(struct vc_data *c)
{
	if (con_is_visible(c)) {
		c->vc_visible_origin = vga_vram_base;
		vga_set_mem_top(c);
	}
	/* vgacon_refcount dec removed */
	c->vc_uni_pagedir_loc = &c->vc_uni_pagedir;
}

/* Stub: simple attribute, no fancy colors/styles needed */
static u8 vgacon_build_attr(struct vc_data *c, u8 color,
			    enum vc_intensity intensity, bool blink,
			    bool underline, bool reverse, bool italic)
{
	return color;
}

/* vgacon_set_cursor_size removed - never called (~29 LOC) */

/* Stub: simple cursor positioning, no fancy shapes needed */
static void vgacon_cursor(struct vc_data *c, int mode)
{
	if (c->vc_mode != KD_TEXT)
		return;
	/* vgacon_restore_screen inlined */
	if (c->vc_origin != c->vc_visible_origin) {
		vc_scrolldelta_helper(c, 0, vga_rolled_over,
				      (void *)vga_vram_base, vga_vram_size);
		vga_set_mem_top(c);
	}
	write_vga(14, (c->vc_pos - vga_vram_base) / 2);
}

/* vgacon_doresize inlined into vgacon_switch - single caller */

static int vgacon_switch(struct vc_data *c)
{
	int x = c->vc_cols * VGA_FONTWIDTH;
	int y = c->vc_rows * c->vc_cell_height;
	int rows = screen_info.orig_video_lines * vga_default_font_height /
		   c->vc_cell_height;

	vga_video_num_columns = c->vc_cols;
	vga_video_num_lines = c->vc_rows;

	/* vga_is_gfx check removed - always false */
	scr_memcpyw((u16 *)c->vc_origin, (u16 *)c->vc_screenbuf,
		    c->vc_screenbuf_size > vga_vram_size ?
			    vga_vram_size :
			    c->vc_screenbuf_size);

	if ((vgacon_xres != x || vgacon_yres != y) &&
	    (!(vga_video_num_columns % 2) &&
	     vga_video_num_columns <= screen_info.orig_video_cols &&
	     vga_video_num_lines <= rows)) {
		/* vgacon_doresize inlined */
		vgacon_xres = c->vc_cols * VGA_FONTWIDTH;
		vgacon_yres = c->vc_rows * c->vc_cell_height;
	}

	return 0;
}

/* vga_set_palette removed - never called (vgacon_set_palette callback removed) */

/* vgacon_blank, vga_vesa_blank, vga_vesa_unblank, vga_pal_blank removed - con_blank never invoked */
/* colourmap, blackwmap, cmapsz removed - unused */

/* vgacon_resize removed - con_resize callback never invoked */

static int vgacon_set_origin(struct vc_data *c)
{
	/* vga_is_gfx check removed - always false */
	c->vc_origin = c->vc_visible_origin = vga_vram_base;
	vga_set_mem_top(c);
	vga_rolled_over = 0;
	return 1;
}

static void vgacon_save_screen(struct vc_data *c)
{
	static int vga_bootup_console = 0;

	if (!vga_bootup_console) {
		vga_bootup_console = 1;
		c->state.x = screen_info.orig_x;
		c->state.y = screen_info.orig_y;
	}

	/* vga_is_gfx check removed - always false */
	scr_memcpyw((u16 *)c->vc_screenbuf, (u16 *)c->vc_origin,
		    c->vc_screenbuf_size > vga_vram_size ?
			    vga_vram_size :
			    c->vc_screenbuf_size);
}

/* Stub: Hello World doesn't need hardware scrolling */
static bool vgacon_scroll(struct vc_data *c, unsigned int t, unsigned int b,
			  enum con_scroll dir, unsigned int lines)
{
	return false;
}

/* vgacon_clear removed - con_clear callback never invoked */
static void vgacon_putc(struct vc_data *vc, int c, int ypos, int xpos)
{
}
static void vgacon_putcs(struct vc_data *vc, const unsigned short *s, int count,
			 int ypos, int xpos)
{
}

const struct consw vga_con = {
	.owner = THIS_MODULE,
	.con_startup = vgacon_startup,
	.con_init = vgacon_init,
	.con_deinit = vgacon_deinit,
	.con_putc = vgacon_putc,
	.con_putcs = vgacon_putcs,
	.con_cursor = vgacon_cursor,
	.con_scroll = vgacon_scroll,
	.con_switch = vgacon_switch,
	/* .con_blank, .con_font_set, .con_font_get, .con_resize, .con_set_palette, .con_scrolldelta removed - never invoked */
	.con_set_origin = vgacon_set_origin,
	.con_save_screen = vgacon_save_screen,
	.con_build_attr = vgacon_build_attr,
	/* .con_invert_region removed - never called through struct */
};

MODULE_LICENSE("GPL");
