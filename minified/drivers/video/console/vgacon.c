
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
#include <linux/init.h>
#include <linux/screen_info.h>
#include <asm/io.h>
#include <asm/vga.h>

/* Inlined from video/vga.h */
#define VGA_CRT_DC 0x3D5
#define VGA_CRT_DM 0x3B5
#define VGA_ATT_W 0x3C0
#define VGA_IS1_RC 0x3DA
#define VGA_PEL_D 0x3C9
#define VGA_PEL_MSK 0x3C6
#define VGA_CRT_IC 0x3D4
#define VGA_CRT_IM 0x3B4
#define VGA_PEL_IW 0x3C8
#define VGA_CRTC_H_DISP 1
#define VGA_CRTC_OVERFLOW 7
#define VGA_CRTC_MAX_SCAN 9
#define VGA_CRTC_CURSOR_START 0x0A
#define VGA_CRTC_CURSOR_END 0x0B
#define VGA_CRTC_V_SYNC_END 0x11
#define VGA_CRTC_V_DISP_END 0x12
#define VGA_CRTC_OFFSET 0x13
#define VGA_CRTC_MODE 0x17

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

static inline void vga_io_w(unsigned short port, unsigned char val)
{
	outb_p(val, port);
}
static inline void vga_mm_w(void __iomem *regbase, unsigned short port,
			    unsigned char val)
{
	writeb(val, regbase + port);
}
static inline void vga_w(void __iomem *regbase, unsigned short port,
			 unsigned char val)
{
	if (regbase)
		vga_mm_w(regbase, port, val);
	else
		vga_io_w(port, val);
}

static DEFINE_RAW_SPINLOCK(vga_lock);
static int cursor_size_lastfrom;
static int cursor_size_lastto;
static u32 vgacon_xres;
static u32 vgacon_yres;
static struct vgastate vgastate;

#define BLANK 0x0020

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
static unsigned long vga_vram_end __read_mostly;
static unsigned int vga_vram_size __read_mostly;
static u16 vga_video_port_reg __read_mostly;
static u16 vga_video_port_val __read_mostly;
static unsigned int vga_video_num_columns;
static unsigned int vga_video_num_lines;
static bool vga_can_do_color;
static unsigned int vga_default_font_height __read_mostly;
static unsigned char vga_video_type __read_mostly;
static int vga_vesa_blanked;
static bool vga_palette_blanked;
static bool vga_is_gfx;
static bool vga_512_chars;
static int vga_video_font_height;
static int vga_scan_lines __read_mostly;
static unsigned int vga_rolled_over;

static bool vga_hardscroll_enabled;
static bool vga_hardscroll_user_enable = true;

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
static void vgacon_restore_screen(struct vc_data *c)
{
	if (c->vc_origin != c->vc_visible_origin) {
		vc_scrolldelta_helper(c, 0, vga_rolled_over,
				      (void *)vga_vram_base, vga_vram_size);
		vga_set_mem_top(c);
	}
}

static const char *vgacon_startup(void)
{
	const char *display_desc = NULL;
	u16 saved1, saved2;
	volatile u16 *p;

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
	vgastate.vgabase = NULL;

	if (screen_info.orig_video_mode == 7) {
		vga_vram_base = 0xb0000;
		vga_video_port_reg = VGA_CRT_IM;
		vga_video_port_val = VGA_CRT_DM;
		if ((screen_info.orig_video_ega_bx & 0xff) != 0x10) {
			static struct resource ega_console_resource = {
				.name = "ega",
				.flags = IORESOURCE_IO,
				.start = 0x3B0,
				.end = 0x3BF
			};
			vga_video_type = VIDEO_TYPE_EGAM;
			vga_vram_size = 0x8000;
			display_desc = "EGA+";
			request_resource(&ioport_resource,
					 &ega_console_resource);
		} else {
			static struct resource mda1_console_resource = {
				.name = "mda",
				.flags = IORESOURCE_IO,
				.start = 0x3B0,
				.end = 0x3BB
			};
			static struct resource mda2_console_resource = {
				.name = "mda",
				.flags = IORESOURCE_IO,
				.start = 0x3BF,
				.end = 0x3BF
			};
			vga_video_type = VIDEO_TYPE_MDA;
			vga_vram_size = 0x2000;
			display_desc = "*MDA";
			request_resource(&ioport_resource,
					 &mda1_console_resource);
			request_resource(&ioport_resource,
					 &mda2_console_resource);
			vga_video_font_height = 14;
		}
	} else {
		vga_can_do_color = true;
		vga_vram_base = 0xb8000;
		vga_video_port_reg = VGA_CRT_IC;
		vga_video_port_val = VGA_CRT_DC;
		if ((screen_info.orig_video_ega_bx & 0xff) != 0x10) {
			int i;

			vga_vram_size = 0x8000;

			if (!screen_info.orig_video_isVGA) {
				static struct resource ega_console_resource = {
					.name = "ega",
					.flags = IORESOURCE_IO,
					.start = 0x3C0,
					.end = 0x3DF
				};
				vga_video_type = VIDEO_TYPE_EGAC;
				display_desc = "EGA";
				request_resource(&ioport_resource,
						 &ega_console_resource);
			} else {
				static struct resource vga_console_resource = {
					.name = "vga+",
					.flags = IORESOURCE_IO,
					.start = 0x3C0,
					.end = 0x3DF
				};
				vga_video_type = VIDEO_TYPE_VGAC;
				display_desc = "VGA+";
				request_resource(&ioport_resource,
						 &vga_console_resource);

				for (i = 0; i < 16; i++) {
					inb_p(VGA_IS1_RC);
					outb_p(i, VGA_ATT_W);
					outb_p(i, VGA_ATT_W);
				}
				outb_p(0x20, VGA_ATT_W);

				for (i = 0; i < 16; i++) {
					outb_p(color_table[i], VGA_PEL_IW);
					outb_p(default_red[i], VGA_PEL_D);
					outb_p(default_grn[i], VGA_PEL_D);
					outb_p(default_blu[i], VGA_PEL_D);
				}
			}
		} else {
			static struct resource cga_console_resource = {
				.name = "cga",
				.flags = IORESOURCE_IO,
				.start = 0x3D4,
				.end = 0x3D5
			};
			vga_video_type = VIDEO_TYPE_CGA;
			vga_vram_size = 0x2000;
			display_desc = "*CGA";
			request_resource(&ioport_resource,
					 &cga_console_resource);
			vga_video_font_height = 8;
		}
	}

	vga_vram_base = VGA_MAP_MEM(vga_vram_base, vga_vram_size);
	vga_vram_end = vga_vram_base + vga_vram_size;

	p = (volatile u16 *)vga_vram_base;
	saved1 = scr_readw(p);
	saved2 = scr_readw(p + 1);
	scr_writew(0xAA55, p);
	scr_writew(0x55AA, p + 1);
	if (scr_readw(p) != 0xAA55 || scr_readw(p + 1) != 0x55AA) {
		scr_writew(saved1, p);
		scr_writew(saved2, p + 1);
		goto no_vga;
	}
	scr_writew(0x55AA, p);
	scr_writew(0xAA55, p + 1);
	if (scr_readw(p) != 0x55AA || scr_readw(p + 1) != 0xAA55) {
		scr_writew(saved1, p);
		scr_writew(saved2, p + 1);
		goto no_vga;
	}
	scr_writew(saved1, p);
	scr_writew(saved2, p + 1);

	if (vga_video_type == VIDEO_TYPE_EGAC ||
	    vga_video_type == VIDEO_TYPE_VGAC ||
	    vga_video_type == VIDEO_TYPE_EGAM) {
		vga_hardscroll_enabled = vga_hardscroll_user_enable;
		vga_default_font_height = screen_info.orig_video_points;
		vga_video_font_height = screen_info.orig_video_points;

		vga_scan_lines = vga_video_font_height * vga_video_num_lines;
	}

	vgacon_xres = screen_info.orig_video_cols * VGA_FONTWIDTH;
	vgacon_yres = vga_scan_lines;

	return display_desc;
}

static void vgacon_init(struct vc_data *c, int init)
{
	struct uni_pagedir *p;

	c->vc_can_do_color = vga_can_do_color;
	c->vc_scan_lines = vga_scan_lines;
	c->vc_font.height = c->vc_cell_height = vga_video_font_height;

	if (init) {
		c->vc_cols = vga_video_num_columns;
		c->vc_rows = vga_video_num_lines;
	} else
		vc_resize(c, vga_video_num_columns, vga_video_num_lines);

	c->vc_complement_mask = 0x7700;
	if (vga_512_chars)
		c->vc_hi_font_mask = 0x0800;
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

static u8 vgacon_build_attr(struct vc_data *c, u8 color,
			    enum vc_intensity intensity, bool blink,
			    bool underline, bool reverse, bool italic)
{
	u8 attr = color;

	if (vga_can_do_color) {
		if (italic)
			attr = (attr & 0xF0) | c->vc_itcolor;
		else if (underline)
			attr = (attr & 0xf0) | c->vc_ulcolor;
		else if (intensity == VCI_HALF_BRIGHT)
			attr = (attr & 0xf0) | c->vc_halfcolor;
	}
	if (reverse)
		attr = ((attr) & 0x88) |
		       ((((attr) >> 4) | ((attr) << 4)) & 0x77);
	if (blink)
		attr ^= 0x80;
	if (intensity == VCI_BOLD)
		attr ^= 0x08;
	if (!vga_can_do_color) {
		if (italic)
			attr = (attr & 0xF8) | 0x02;
		else if (underline)
			attr = (attr & 0xf8) | 0x01;
		else if (intensity == VCI_HALF_BRIGHT)
			attr = (attr & 0xf0) | 0x08;
	}
	return attr;
}

static void vgacon_set_cursor_size(int xpos, int from, int to)
{
	unsigned long flags;
	int curs, cure;

	if ((from == cursor_size_lastfrom) && (to == cursor_size_lastto))
		return;
	cursor_size_lastfrom = from;
	cursor_size_lastto = to;

	raw_spin_lock_irqsave(&vga_lock, flags);
	if (vga_video_type >= VIDEO_TYPE_VGAC) {
		outb_p(VGA_CRTC_CURSOR_START, vga_video_port_reg);
		curs = inb_p(vga_video_port_val);
		outb_p(VGA_CRTC_CURSOR_END, vga_video_port_reg);
		cure = inb_p(vga_video_port_val);
	} else {
		curs = 0;
		cure = 0;
	}

	curs = (curs & 0xc0) | from;
	cure = (cure & 0xe0) | to;

	outb_p(VGA_CRTC_CURSOR_START, vga_video_port_reg);
	outb_p(curs, vga_video_port_val);
	outb_p(VGA_CRTC_CURSOR_END, vga_video_port_reg);
	outb_p(cure, vga_video_port_val);
	raw_spin_unlock_irqrestore(&vga_lock, flags);
}

static void vgacon_cursor(struct vc_data *c, int mode)
{
	if (c->vc_mode != KD_TEXT)
		return;

	vgacon_restore_screen(c);

	switch (mode) {
	case CM_ERASE:
		write_vga(14, (c->vc_pos - vga_vram_base) / 2);
		if (vga_video_type >= VIDEO_TYPE_VGAC)
			vgacon_set_cursor_size(c->state.x, 31, 30);
		else
			vgacon_set_cursor_size(c->state.x, 31, 31);
		break;

	case CM_MOVE:
	case CM_DRAW:
		write_vga(14, (c->vc_pos - vga_vram_base) / 2);
		switch (CUR_SIZE(c->vc_cursor_type)) {
		case CUR_UNDERLINE:
			vgacon_set_cursor_size(
				c->state.x,
				c->vc_cell_height -
					(c->vc_cell_height < 10 ? 2 : 3),
				c->vc_cell_height -
					(c->vc_cell_height < 10 ? 1 : 2));
			break;
		case CUR_TWO_THIRDS:
			vgacon_set_cursor_size(
				c->state.x, c->vc_cell_height / 3,
				c->vc_cell_height -
					(c->vc_cell_height < 10 ? 1 : 2));
			break;
		case CUR_LOWER_THIRD:
			vgacon_set_cursor_size(
				c->state.x, (c->vc_cell_height * 2) / 3,
				c->vc_cell_height -
					(c->vc_cell_height < 10 ? 1 : 2));
			break;
		case CUR_LOWER_HALF:
			vgacon_set_cursor_size(
				c->state.x, c->vc_cell_height / 2,
				c->vc_cell_height -
					(c->vc_cell_height < 10 ? 1 : 2));
			break;
		case CUR_NONE:
			if (vga_video_type >= VIDEO_TYPE_VGAC)
				vgacon_set_cursor_size(c->state.x, 31, 30);
			else
				vgacon_set_cursor_size(c->state.x, 31, 31);
			break;
		default:
			vgacon_set_cursor_size(c->state.x, 1,
					       c->vc_cell_height);
			break;
		}
		break;
	}
}

static int vgacon_doresize(struct vc_data *c, unsigned int width,
			   unsigned int height)
{
	unsigned long flags;
	unsigned int scanlines = height * c->vc_cell_height;
	u8 scanlines_lo = 0, r7 = 0, vsync_end = 0, mode, max_scan;

	raw_spin_lock_irqsave(&vga_lock, flags);

	vgacon_xres = width * VGA_FONTWIDTH;
	vgacon_yres = height * c->vc_cell_height;
	if (vga_video_type >= VIDEO_TYPE_VGAC) {
		outb_p(VGA_CRTC_MAX_SCAN, vga_video_port_reg);
		max_scan = inb_p(vga_video_port_val);

		if (max_scan & 0x80)
			scanlines <<= 1;

		outb_p(VGA_CRTC_MODE, vga_video_port_reg);
		mode = inb_p(vga_video_port_val);

		if (mode & 0x04)
			scanlines >>= 1;

		scanlines -= 1;
		scanlines_lo = scanlines & 0xff;

		outb_p(VGA_CRTC_OVERFLOW, vga_video_port_reg);
		r7 = inb_p(vga_video_port_val) & ~0x42;

		if (scanlines & 0x100)
			r7 |= 0x02;
		if (scanlines & 0x200)
			r7 |= 0x40;

		outb_p(VGA_CRTC_V_SYNC_END, vga_video_port_reg);
		vsync_end = inb_p(vga_video_port_val);
		outb_p(VGA_CRTC_V_SYNC_END, vga_video_port_reg);
		outb_p(vsync_end & ~0x80, vga_video_port_val);
	}

	outb_p(VGA_CRTC_H_DISP, vga_video_port_reg);
	outb_p(width - 1, vga_video_port_val);
	outb_p(VGA_CRTC_OFFSET, vga_video_port_reg);
	outb_p(width >> 1, vga_video_port_val);

	if (vga_video_type >= VIDEO_TYPE_VGAC) {
		outb_p(VGA_CRTC_V_DISP_END, vga_video_port_reg);
		outb_p(scanlines_lo, vga_video_port_val);
		outb_p(VGA_CRTC_OVERFLOW, vga_video_port_reg);
		outb_p(r7, vga_video_port_val);

		outb_p(VGA_CRTC_V_SYNC_END, vga_video_port_reg);
		outb_p(vsync_end, vga_video_port_val);
	}

	raw_spin_unlock_irqrestore(&vga_lock, flags);
	return 0;
}

static int vgacon_switch(struct vc_data *c)
{
	int x = c->vc_cols * VGA_FONTWIDTH;
	int y = c->vc_rows * c->vc_cell_height;
	int rows = screen_info.orig_video_lines * vga_default_font_height /
		   c->vc_cell_height;

	vga_video_num_columns = c->vc_cols;
	vga_video_num_lines = c->vc_rows;

	if (!vga_is_gfx) {
		scr_memcpyw((u16 *)c->vc_origin, (u16 *)c->vc_screenbuf,
			    c->vc_screenbuf_size > vga_vram_size ?
				    vga_vram_size :
				    c->vc_screenbuf_size);

		if ((vgacon_xres != x || vgacon_yres != y) &&
		    (!(vga_video_num_columns % 2) &&
		     vga_video_num_columns <= screen_info.orig_video_cols &&
		     vga_video_num_lines <= rows))
			vgacon_doresize(c, c->vc_cols, c->vc_rows);
	}

	return 0;
}

static void vga_set_palette(struct vc_data *vc, const unsigned char *table)
{
	int i, j;

	vga_w(vgastate.vgabase, VGA_PEL_MSK, 0xff);
	for (i = j = 0; i < 16; i++) {
		vga_w(vgastate.vgabase, VGA_PEL_IW, table[i]);
		vga_w(vgastate.vgabase, VGA_PEL_D, vc->vc_palette[j++] >> 2);
		vga_w(vgastate.vgabase, VGA_PEL_D, vc->vc_palette[j++] >> 2);
		vga_w(vgastate.vgabase, VGA_PEL_D, vc->vc_palette[j++] >> 2);
	}
}

/* vgacon_set_palette removed - con_set_palette callback never invoked */

/* vgacon_blank, vga_vesa_blank, vga_vesa_unblank, vga_pal_blank removed - con_blank never invoked */
/* colourmap, blackwmap, cmapsz removed - unused */

/* vgacon_resize removed - con_resize callback never invoked */

static int vgacon_set_origin(struct vc_data *c)
{
	if (vga_is_gfx || (console_blanked && !vga_palette_blanked))
		return 0;
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

	if (!vga_is_gfx)
		scr_memcpyw((u16 *)c->vc_screenbuf, (u16 *)c->vc_origin,
			    c->vc_screenbuf_size > vga_vram_size ?
				    vga_vram_size :
				    c->vc_screenbuf_size);
}

static bool vgacon_scroll(struct vc_data *c, unsigned int t, unsigned int b,
			  enum con_scroll dir, unsigned int lines)
{
	unsigned long oldo;
	unsigned int delta;

	if (t || b != c->vc_rows || vga_is_gfx || c->vc_mode != KD_TEXT)
		return false;

	if (!vga_hardscroll_enabled || lines >= c->vc_rows / 2)
		return false;

	vgacon_restore_screen(c);
	oldo = c->vc_origin;
	delta = lines * c->vc_size_row;
	if (dir == SM_UP) {
		if (c->vc_scr_end + delta >= vga_vram_end) {
			scr_memcpyw((u16 *)vga_vram_base, (u16 *)(oldo + delta),
				    c->vc_screenbuf_size - delta);
			c->vc_origin = vga_vram_base;
			vga_rolled_over = oldo - vga_vram_base;
		} else
			c->vc_origin += delta;
		scr_memsetw(
			(u16 *)(c->vc_origin + c->vc_screenbuf_size - delta),
			c->vc_video_erase_char, delta);
	} else {
		if (oldo - delta < vga_vram_base) {
			scr_memmovew((u16 *)(vga_vram_end -
					     c->vc_screenbuf_size + delta),
				     (u16 *)oldo, c->vc_screenbuf_size - delta);
			c->vc_origin = vga_vram_end - c->vc_screenbuf_size;
			vga_rolled_over = 0;
		} else
			c->vc_origin -= delta;
		c->vc_scr_end = c->vc_origin + c->vc_screenbuf_size;
		scr_memsetw((u16 *)(c->vc_origin), c->vc_video_erase_char,
			    delta);
	}
	c->vc_scr_end = c->vc_origin + c->vc_screenbuf_size;
	c->vc_visible_origin = c->vc_origin;
	vga_set_mem_top(c);
	c->vc_pos = (c->vc_pos - oldo) + c->vc_origin;
	return true;
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
