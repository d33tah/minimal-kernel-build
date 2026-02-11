
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched/signal.h>
#include <linux/tty.h>
#include <linux/tty_buffer.h>
#include <linux/tty_port.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
/* inlined from linux/kd.h */
#ifndef KD_TEXT
#define KD_TEXT 0x00
#define KD_GRAPHICS 0x01
#endif
#include <linux/slab.h>
/* linux/vmalloc.h removed - no vmalloc functions */
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/vt_kern.h>
#include <linux/selection.h> /* includes tiocl defines */
/* linux/tty.h already included above */
/* linux/interrupt.h removed - no interrupt features used */
/* consolemap.h inlined into vt_kern.h */
/* duplicate linux/interrupt.h removed */
#include <linux/workqueue.h>

#include <linux/bitops.h>
/* notifier.h removed - notifiers not used */

/* vc_SAK removed - work was initialized but never scheduled */
#include <linux/device.h>
/* io.h removed - I/O port functions not used */
#include <linux/uaccess.h>

/* ctype.h removed - character classification not used */

#define MAX_NR_CON_DRIVER 16

#define CON_DRIVER_FLAG_INIT 2

struct con_driver {
	const struct consw *con;
	const char *desc;
	struct device *dev;
	int node;
	int first;
	int last;
	int flag;
};

static struct con_driver registered_con_driver[MAX_NR_CON_DRIVER];
const struct consw *conswitchp;

/* DEFAULT_BELL_PITCH, DEFAULT_BELL_DURATION, DEFAULT_CURSOR_BLINK_MS removed - unused */

struct vc vc_cons[MAX_NR_CONSOLES];

#ifndef VT_SINGLE_DRIVER
static const struct consw *con_driver_map[MAX_NR_CONSOLES];
#endif

static void vc_init(struct vc_data *vc, unsigned int rows, unsigned int cols,
		    int do_clear);
static void gotoxy(struct vc_data *vc, int new_x, int new_y);
/* save_cur forward decl removed - inlined */
static void reset_terminal(struct vc_data *vc, int do_clear);
/* set_cursor, hide_cursor forward decls removed - now trivial stubs defined above */

static int printable;

int fg_console;
/* last_console removed - never read or written */
/* want_console removed - never read */

static struct vc_data *master_display_fg;

/* console_blank_hook, console_timer removed - never used */
/* blank_state and blank_* enum removed - was never read */
/* tty0dev removed - write-only variable, value never used after assignment */

/* Removed: vt_notifier_list, notify_write, notify_update - empty stubs */

/* con_is_fg inlined - returned vc->vc_num == fg_console */

static inline bool con_should_update(const struct vc_data *vc)
{
	/* console_blanked check removed - never assigned, always 0 */
	return con_is_visible(vc);
}

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
	/* con_getxy is never set, so always use this path */
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
		/* con_getxy check removed - never set (~4 LOC) */
	}
}

/* build_attr removed - never called in minimal kernel */

static void update_attr(struct vc_data *vc)
{
	vc->vc_attr = vc->state.color;
	vc->vc_video_erase_char = ' ' | (vc->state.color << 8);
}

/* Stubbed cursor functions - minimal kernel doesn't need cursor display */
static void hide_cursor(struct vc_data *vc)
{
}
static void set_cursor(struct vc_data *vc)
{
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

/* save_screen inlined into con_init */
/* flush_scrollback, redraw_screen inlined into con_init (~6 LOC) */

int vc_cons_allocated(unsigned int i)
{
	return (i < MAX_NR_CONSOLES && vc_cons[i].d);
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

/* visual_deinit inlined */

/* vc_port_destruct, vc_port_ops, vc_allocate removed - only used by dead con_ops path */

/* vc_do_resize inlined - was a stub returning 0 */

int vc_resize(struct vc_data *vc, unsigned int cols, unsigned int rows)
{
	/* Minimal stub: static console doesn't need resize */
	return 0;
}

/* vt_resize removed - ops->resize never called */

/* EP enum removed - vc_priv removed (was write-only) */

/* set_kbd, clr_kbd, is_kbd, decarm, decckm, kbdapplic, lnm removed - unused */

const unsigned char color_table[] = { 0, 4,  2,	 6,  1, 5,  3,	7,
				      8, 12, 10, 14, 9, 13, 11, 15 };

/* default_red/grn/blu arrays removed - all zeros, replaced with memset */

/* Simplified gotoxy - vc_decom always 0 (origin mode disabled) */
static void gotoxy(struct vc_data *vc, int new_x, int new_y)
{
	if (new_x < 0)
		vc->state.x = 0;
	else if (new_x >= vc->vc_cols)
		vc->state.x = vc->vc_cols - 1;
	else
		vc->state.x = new_x;

	/* vc_decom is always 0, so min_y=0 and max_y=vc_rows */
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
	if (con_should_update(vc))
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

/* vc_draw_region removed - only used in dead con_write */

/* console_driver removed - never initialized (vty_init removed) */

static void vt_console_print(struct console *co, const char *b, unsigned count)
{
	struct vc_data *vc = vc_cons[fg_console].d;
	static DEFINE_SPINLOCK(printing_lock);
	const ushort *start;
	ushort start_x;

	if (!printable || !spin_trylock(&printing_lock))
		return;

	if (!vc_cons_allocated(fg_console))
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

/* vt_console_device removed - console_device() never called */

static struct console vt_console_driver = {
	.name = "tty",
	.write = vt_console_print,
	.flags = CON_PRINTBUFFER,
	.index = -1,
};

/* con_write, con_flush_chars, con_install, con_open, con_shutdown, con_cleanup
   all removed - only referenced from dead con_ops struct */

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

	for (i = 0; i < MAX_NR_CON_DRIVER; i++) {
		struct con_driver *con_driver = &registered_con_driver[i];

		if (con_driver->con == NULL) {
			con_driver->con = conswitchp;
			con_driver->desc = display_desc;
			con_driver->flag = CON_DRIVER_FLAG_INIT;
			con_driver->first = 0;
			con_driver->last = MAX_NR_CONSOLES - 1;
			break;
		}
	}

	for (i = 0; i < MAX_NR_CONSOLES; i++)
		con_driver_map[i] = conswitchp;

	/* blankinterval check removed - always 0 */

	for (currcons = 0; currcons < MIN_NR_CONSOLES; currcons++) {
		vc_cons[currcons].d = vc =
			kzalloc(sizeof(struct vc_data), GFP_NOWAIT);
		/* INIT_WORK SAK_work removed - never scheduled */
		tty_port_init(&vc->port);
		visual_init(vc, currcons, 1);

		vc->vc_screenbuf = kzalloc(vc->vc_screenbuf_size, GFP_NOWAIT);
		vc_init(vc, vc->vc_rows, vc->vc_cols,
			currcons || !vc->vc_sw->con_save_screen);
	}
	currcons = fg_console = 0;
	master_display_fg = vc = vc_cons[currcons].d;
	set_origin(vc);
	/* save_screen inlined */
	WARN_CONSOLE_UNLOCKED();
	if (vc->vc_sw->con_save_screen)
		vc->vc_sw->con_save_screen(vc);
	gotoxy(vc, vc->state.x, vc->state.y);
	csi_J(vc, 0);
	/* Inlined redraw_screen */
	{
		int update;
		int old_was_color;

		hide_cursor(vc);

		old_was_color = vc->vc_can_do_color;
		set_origin(vc);
		update = vc->vc_sw->con_switch(vc);

		if (old_was_color != vc->vc_can_do_color)
			update_attr(vc);

		if (update && vc->vc_mode != KD_GRAPHICS)
			do_update_region(vc, vc->vc_origin,
					 vc->vc_screenbuf_size / 2);

		set_cursor(vc);
	}
	printable = 1;

	console_unlock();

	register_console(&vt_console_driver);
	return 0;
}
console_initcall(con_init);

/* con_ops struct removed - vty_init was removed, so no tty_driver ever uses it */

#ifndef VT_SINGLE_DRIVER

/* do_bind_con_driver, vt_bind, vt_unbind, store_bind, show_bind, show_name,
   dev_attr_bind, dev_attr_name, con_dev_attrs, vtconsole_init_device,
   vtconsole_class, con_is_bound - all removed, never used */

bool con_is_visible(const struct vc_data *vc)
{
	WARN_CONSOLE_UNLOCKED();

	return *vc->vc_display_fg == vc;
}

/* vtconsole_class_init removed - empty initcall (~5 LOC) */

#endif
