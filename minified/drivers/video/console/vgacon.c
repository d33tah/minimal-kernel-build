
#include <linux/module.h>
#include <linux/console.h>
#include <linux/vt_kern.h>
static inline void scr_memcpyw(u16 *d, const u16 *s, unsigned int count)
{
	memcpy(d, s, count);
}
#include <linux/ioport.h>
#include <asm/io.h>
#define VGA_MAP_MEM(x, s) ((unsigned long)phys_to_virt(x))

#define VGA_CRT_IC 0x3D4

static DEFINE_RAW_SPINLOCK(vga_lock);

static const char *vgacon_startup(void);
static void vgacon_init(struct vc_data *c, int init);
static int vgacon_switch(struct vc_data *c);
static int vgacon_set_origin(struct vc_data *c);
static void vgacon_save_screen(struct vc_data *c);

static unsigned long vga_vram_base __read_mostly;
static unsigned int vga_vram_size __read_mostly;
static u16 vga_video_port_reg __read_mostly;
static unsigned int vga_video_num_columns;
static unsigned int vga_video_num_lines;
static int vga_video_font_height;

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

	{
		static struct resource vga_console_resource = {
			.name = "vga+",
			.flags = IORESOURCE_IO,
			.start = 0x3C0,
			.end = 0x3DF
		};

		vga_vram_base = 0xb8000;
		vga_video_port_reg = VGA_CRT_IC;
		vga_vram_size = 0x8000;
		display_desc = "VGA+";
		request_resource(&ioport_resource, &vga_console_resource);
	}

	vga_vram_base = VGA_MAP_MEM(vga_vram_base, vga_vram_size);

	vga_video_font_height = screen_info.orig_video_points;

	return display_desc;
}

static void vgacon_init(struct vc_data *c, int init)
{
	c->vc_cell_height = vga_video_font_height;

	if (init) {
		c->vc_cols = vga_video_num_columns;
		c->vc_rows = vga_video_num_lines;
	} else
		vc_resize(c, vga_video_num_columns, vga_video_num_lines);
}

static int vgacon_switch(struct vc_data *c)
{
	vga_video_num_columns = c->vc_cols;
	vga_video_num_lines = c->vc_rows;

	scr_memcpyw((u16 *)c->vc_origin, (u16 *)c->vc_screenbuf,
		    c->vc_screenbuf_size > vga_vram_size ?
			    vga_vram_size :
			    c->vc_screenbuf_size);

	return 0;
}

static int vgacon_set_origin(struct vc_data *c)
{
	c->vc_origin = c->vc_visible_origin = vga_vram_base;
	vga_set_mem_top(c);
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

const struct consw vga_con = {
	.owner = THIS_MODULE,
	.con_startup = vgacon_startup,
	.con_init = vgacon_init,
	.con_scroll = vgacon_scroll,
	.con_switch = vgacon_switch,
	.con_set_origin = vgacon_set_origin,
	.con_save_screen = vgacon_save_screen,
};

MODULE_LICENSE("GPL");
