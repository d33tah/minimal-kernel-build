#include <linux/console.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <video/vga.h>

/*
 * VGA text-mode console: finds the text buffer the BIOS left on screen and
 * moves the hardware cursor. The vt layer writes characters, and scrolls,
 * directly in this buffer (vc_origin is its first cell). The font, palette,
 * cursor shape and video mode are left as the BIOS set them.
 */
static unsigned long vga_vram_base;
static u16 vga_video_port_reg;

static void write_vga(unsigned char reg, unsigned int val)
{
	outw(reg + (val & 0xff00), vga_video_port_reg);
	outw(reg + 1 + ((val << 8) & 0xff00), vga_video_port_reg);
}

static bool vgacon_probe_vram(volatile u16 *p)
{
	u16 saved1 = scr_readw(p), saved2 = scr_readw(p + 1);
	bool ok;

	scr_writew(0xAA55, p);
	scr_writew(0x55AA, p + 1);
	ok = scr_readw(p) == 0xAA55 && scr_readw(p + 1) == 0x55AA;
	scr_writew(0x55AA, p);
	scr_writew(0xAA55, p + 1);
	ok = ok && scr_readw(p) == 0x55AA && scr_readw(p + 1) == 0xAA55;
	scr_writew(saved1, p);
	scr_writew(saved2, p + 1);
	return ok;
}

static const char *vgacon_startup(void)
{
	u8 mode = screen_info.orig_video_mode;

	if (screen_info.orig_video_isVGA == VIDEO_TYPE_VLFB ||
	    screen_info.orig_video_isVGA == VIDEO_TYPE_EFI ||
	    !screen_info.orig_video_lines || !screen_info.orig_video_cols)
		return NULL;

	/* Graphics modes have no text buffer. */
	if (mode == 0x0D || mode == 0x0E || mode == 0x10 || mode == 0x12 ||
	    mode == 0x6A)
		return NULL;

	if (mode == 7) {
		vga_vram_base = VGA_MAP_MEM(0xb0000, 0x8000);
		vga_video_port_reg = VGA_CRT_IM;
	} else {
		vga_vram_base = VGA_MAP_MEM(0xb8000, 0x8000);
		vga_video_port_reg = VGA_CRT_IC;
	}

	if (!vgacon_probe_vram((volatile u16 *)vga_vram_base))
		return NULL;

	/* Show the buffer from its first cell. */
	write_vga(12, 0);
	return mode == 7 ? "MDA/EGA mono" : "VGA+";
}

static void vgacon_init(struct vc_data *c)
{
	c->vc_cols = screen_info.orig_video_cols;
	c->vc_rows = screen_info.orig_video_lines;
	c->vc_origin = vga_vram_base;
	c->vc_x = screen_info.orig_x;
	c->vc_y = screen_info.orig_y;
}

static void vgacon_cursor(struct vc_data *c)
{
	write_vga(14, (c->vc_pos - vga_vram_base) / 2);
}

const struct consw vga_con = {
	.con_startup = vgacon_startup,
	.con_init = vgacon_init,
	.con_cursor = vgacon_cursor,
};
