
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#ifndef KD_TEXT
#define KD_TEXT 0x00
#define KD_GRAPHICS 0x01
#endif
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/vt_kern.h>
#define scr_writew(val, addr) (*(addr) = (val))
#define scr_readw(addr) (*(addr))
static inline void scr_memsetw(u16 *s, u16 c, unsigned int count)
{
	memset16(s, c, count / 2);
}
static inline void scr_memmovew(u16 *d, const u16 *s, unsigned int count)
{
	memmove(d, s, count);
}
extern const unsigned char color_table[];
/* linux/tty.h already included above */
#include <linux/workqueue.h>

#include <linux/bitops.h>

#include <linux/device.h>

const struct consw *conswitchp;

struct vc vc_cons[MAX_NR_CONSOLES];

#ifndef VT_SINGLE_DRIVER
static const struct consw *con_driver_map[MAX_NR_CONSOLES];
#endif

static void vc_init(struct vc_data *vc, unsigned int rows, unsigned int cols,
		    int do_clear);
static void gotoxy(struct vc_data *vc, int new_x, int new_y);
static void reset_terminal(struct vc_data *vc, int do_clear);

static int printable;

int fg_console;

static struct vc_data *master_display_fg;

static void con_scroll(struct vc_data *vc, unsigned int t, unsigned int b,
		       enum con_scroll dir, unsigned int nr)
{
	u16 *clear, *d, *s;

	if (t + nr >= b)
		nr = b - t - 1;
	if (b > vc->vc_rows || t >= b || nr < 1)
		return;
	if (con_is_visible(vc) && vc->vc_sw->con_scroll(vc, t, b, dir, nr))
		return;

	s = clear = (u16 *)(vc->vc_origin + vc->vc_size_row * t);
	d = (u16 *)(vc->vc_origin + vc->vc_size_row * (t + nr));

	if (dir == SM_UP) {
		clear = s + (b - t - nr) * vc->vc_cols;
		swap(s, d);
	}
	scr_memmovew(d, s, (b - t - nr) * vc->vc_size_row);
	scr_memsetw(clear, vc->vc_video_erase_char, vc->vc_size_row * nr);
}

static void do_update_region(struct vc_data *vc, unsigned long start, int count)
{
	unsigned int xx, yy, offset;
	u16 *p;

	p = (u16 *)start;
	offset = (start - vc->vc_origin) / 2;
	xx = offset % vc->vc_cols;
	yy = offset / vc->vc_cols;
	for (;;) {
		u16 attrib = scr_readw(p) & 0xff00;
		int startx = xx;
		u16 *q = p;
		while (xx < vc->vc_cols && count) {
			if (attrib != (scr_readw(p) & 0xff00)) {
				if (p > q)
					vc->vc_sw->con_putcs(vc, q, p - q, yy,
							     startx);
				startx = xx;
				q = p;
				attrib = scr_readw(p) & 0xff00;
			}
			p++;
			xx++;
			count--;
		}
		if (p > q)
			vc->vc_sw->con_putcs(vc, q, p - q, yy, startx);
		if (!count)
			break;
		xx = 0;
		yy++;
	}
}

static void update_attr(struct vc_data *vc)
{
	vc->vc_attr = vc->state.color;
	vc->vc_video_erase_char = ' ' | (vc->state.color << 8);
}

static void set_origin(struct vc_data *vc)
{
	if (!con_is_visible(vc) || !vc->vc_sw->con_set_origin ||
	    !vc->vc_sw->con_set_origin(vc))
		vc->vc_origin = (unsigned long)vc->vc_screenbuf;
	vc->vc_visible_origin = vc->vc_origin;
	vc->vc_scr_end = vc->vc_origin + vc->vc_screenbuf_size;
	vc->vc_pos =
		vc->vc_origin + vc->vc_size_row * vc->state.y + 2 * vc->state.x;
}

static void visual_init(struct vc_data *vc, int num, int init)
{
	if (vc->vc_sw)
		module_put(vc->vc_sw->owner);
	vc->vc_sw = conswitchp;
#ifndef VT_SINGLE_DRIVER
	if (con_driver_map[num])
		vc->vc_sw = con_driver_map[num];
#endif
	__module_get(vc->vc_sw->owner);
	vc->vc_num = num;
	vc->vc_display_fg = &master_display_fg;
	vc->vc_uni_pagedir_loc = &vc->vc_uni_pagedir;
	vc->vc_uni_pagedir = NULL;
	vc->vc_complement_mask = 0;
	vc->vc_can_do_color = 0;
	vc->vc_sw->con_init(vc, init);
	if (!vc->vc_complement_mask)
		vc->vc_complement_mask = vc->vc_can_do_color ? 0x7700 : 0x0800;
	vc->vc_s_complement_mask = vc->vc_complement_mask;
	vc->vc_size_row = vc->vc_cols << 1;
	vc->vc_screenbuf_size = vc->vc_rows * vc->vc_size_row;
}

int vc_resize(struct vc_data *vc, unsigned int cols, unsigned int rows)
{
	/* Minimal stub: static console doesn't need resize */
	return 0;
}

const unsigned char color_table[] = { 0, 4,  2,	 6,  1, 5,  3,	7,
				      8, 12, 10, 14, 9, 13, 11, 15 };

static void gotoxy(struct vc_data *vc, int new_x, int new_y)
{
	if (new_x < 0)
		vc->state.x = 0;
	else if (new_x >= vc->vc_cols)
		vc->state.x = vc->vc_cols - 1;
	else
		vc->state.x = new_x;

	if (new_y < 0)
		vc->state.y = 0;
	else if (new_y >= vc->vc_rows)
		vc->state.y = vc->vc_rows - 1;
	else
		vc->state.y = new_y;
	vc->vc_pos = vc->vc_origin + vc->state.y * vc->vc_size_row +
		     (vc->state.x << 1);
	vc->vc_need_wrap = 0;
}

static void lf(struct vc_data *vc)
{
	if (vc->state.y + 1 == vc->vc_bottom)
		con_scroll(vc, vc->vc_top, vc->vc_bottom, SM_UP, 1);
	else if (vc->state.y < vc->vc_rows - 1) {
		vc->state.y++;
		vc->vc_pos += vc->vc_size_row;
	}
	vc->vc_need_wrap = 0;
}

static inline void cr(struct vc_data *vc)
{
	vc->vc_pos -= vc->state.x << 1;
	vc->vc_need_wrap = vc->state.x = 0;
}

static void csi_J(struct vc_data *vc, int vpar)
{
	unsigned int count;
	unsigned short *start;

	if (vpar == 0) {
		count = (vc->vc_scr_end - vc->vc_pos) >> 1;
		start = (unsigned short *)vc->vc_pos;
	} else {
		count = vc->vc_cols * vc->vc_rows;
		start = (unsigned short *)vc->vc_origin;
	}
	scr_memsetw(start, vc->vc_video_erase_char, 2 * count);
	if (con_is_visible(vc))
		do_update_region(vc, (unsigned long)start, count);
	vc->vc_need_wrap = 0;
}

static void reset_terminal(struct vc_data *vc, int do_clear)
{
	vc->vc_top = 0;
	vc->vc_bottom = vc->vc_rows;
	vc->vc_state = 0;
	vc->vc_need_wrap = 0;
	vc->vc_complement_mask = vc->vc_s_complement_mask;

	vc->state.color = vc->vc_def_color;
	update_attr(vc);

	gotoxy(vc, 0, 0);
	if (do_clear)
		csi_J(vc, 2);
}

static void vt_console_print(struct console *co, const char *b, unsigned count)
{
	struct vc_data *vc = vc_cons[fg_console].d;
	static DEFINE_SPINLOCK(printing_lock);
	const ushort *start;
	ushort start_x;

	if (!printable || !spin_trylock(&printing_lock))
		return;

	if (!(fg_console < MAX_NR_CONSOLES && vc_cons[fg_console].d))
		goto quit;

	start = (ushort *)vc->vc_pos;
	start_x = vc->state.x;

	while (count--) {
		unsigned char c = *b++;
		if (c == '\n') {
			lf(vc);
			cr(vc);
		} else {
			scr_writew((vc->vc_attr << 8) + c,
				   (unsigned short *)vc->vc_pos);
			vc->vc_pos += 2;
			vc->state.x++;
		}
	}
	if (con_is_visible(vc))
		vc->vc_sw->con_putcs(vc, start, count, vc->state.y, start_x);

quit:
	spin_unlock(&printing_lock);
}

static struct console vt_console_driver = {
	.name = "tty",
	.write = vt_console_print,
	.flags = CON_PRINTBUFFER,
	.index = -1,
};

static int default_color = 7;

static void vc_init(struct vc_data *vc, unsigned int rows, unsigned int cols,
		    int do_clear)
{
	vc->vc_cols = cols;
	vc->vc_rows = rows;
	vc->vc_size_row = cols << 1;
	vc->vc_screenbuf_size = vc->vc_rows * vc->vc_size_row;

	set_origin(vc);
	vc->vc_pos = vc->vc_origin;
	vc->vc_mode = KD_TEXT;
	vc->vc_def_color = default_color;
	reset_terminal(vc, do_clear);
}

static int __init con_init(void)
{
	const char *display_desc = NULL;
	struct vc_data *vc;
	unsigned int currcons = 0, i;

	console_lock();

	if (!conswitchp)
		conswitchp = &dummy_con;
	display_desc = conswitchp->con_startup();
	if (!display_desc) {
		fg_console = 0;
		console_unlock();
		return 0;
	}

	for (i = 0; i < MAX_NR_CONSOLES; i++)
		con_driver_map[i] = conswitchp;

	for (currcons = 0; currcons < MIN_NR_CONSOLES; currcons++) {
		vc_cons[currcons].d = vc =
			kzalloc(sizeof(struct vc_data), GFP_NOWAIT);
		tty_port_init(&vc->port);
		visual_init(vc, currcons, 1);

		vc->vc_screenbuf = kzalloc(vc->vc_screenbuf_size, GFP_NOWAIT);
		vc_init(vc, vc->vc_rows, vc->vc_cols,
			currcons || !vc->vc_sw->con_save_screen);
	}
	currcons = fg_console = 0;
	master_display_fg = vc = vc_cons[currcons].d;
	set_origin(vc);
	WARN_CONSOLE_UNLOCKED();
	if (vc->vc_sw->con_save_screen)
		vc->vc_sw->con_save_screen(vc);
	gotoxy(vc, vc->state.x, vc->state.y);
	csi_J(vc, 0);
	set_origin(vc);
	if (vc->vc_sw->con_switch(vc) && vc->vc_mode != KD_GRAPHICS)
		do_update_region(vc, vc->vc_origin, vc->vc_screenbuf_size / 2);
	printable = 1;

	console_unlock();

	register_console(&vt_console_driver);
	return 0;
}
console_initcall(con_init);

#ifndef VT_SINGLE_DRIVER

bool con_is_visible(const struct vc_data *vc)
{
	WARN_CONSOLE_UNLOCKED();

	return *vc->vc_display_fg == vc;
}

#endif
