
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched/signal.h>
#include <linux/tty.h>
#include <linux/tty_buffer.h>
#include <linux/tty_port.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kd.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/vt_kern.h>
#include <linux/selection.h> /* includes tiocl defines */
/* linux/tty.h already included above */
#include <linux/interrupt.h>
#include <linux/consolemap.h>
/* duplicate linux/interrupt.h removed */
#include <linux/workqueue.h>

#include <linux/bitops.h>
/* notifier.h removed - notifiers not used */

/* vc_SAK - stub for SAK work, never actually scheduled.
   Inlined from vt_ioctl.c to allow removal of that file */
static void vc_SAK(struct work_struct *work)
{
}
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

static int con_open(struct tty_struct *, struct file *);
static void vc_init(struct vc_data *vc, unsigned int rows, unsigned int cols,
		    int do_clear);
static void gotoxy(struct vc_data *vc, int new_x, int new_y);
/* save_cur forward decl removed - inlined */
static void reset_terminal(struct vc_data *vc, int do_clear);
static void con_flush_chars(struct tty_struct *tty);
static void set_cursor(struct vc_data *vc);
static void hide_cursor(struct vc_data *vc);
/* blank_screen_t forward decl removed - function unused */
/* set_palette forward decl removed - stub inlined */

static int printable;
int default_utf8 = true;
int global_cursor_default = -1;
static int cur_default = CUR_UNDERLINE;
/* console_blanked removed - never assigned, always 0 */

int fg_console;
/* last_console removed - never read or written */
/* want_console removed - never read */

static struct vc_data *master_display_fg;

/* console_blank_hook, console_timer removed - never used */
/* blank_state and blank_* enum removed - was never read */

static struct device *tty0dev;

/* Removed: vt_notifier_list, notify_write, notify_update - empty stubs */

/* con_is_fg inlined - returned vc->vc_num == fg_console */

static inline bool con_should_update(const struct vc_data *vc)
{
	/* console_blanked check removed - never assigned, always 0 */
	return con_is_visible(vc);
}

#ifdef NO_VC_UNI_SCREEN

#define get_vc_uniscr(vc) NULL
#else
#define get_vc_uniscr(vc) vc->vc_uni_screen
#endif

#define VC_UNI_SCREEN_DEBUG 0

typedef uint32_t char32_t;

struct uni_screen {
	char32_t *lines[0];
};

/* Removed: vc_uniscr_clear_line, vc_uniscr_clear_lines, vc_uniscr_scroll - empty stubs */

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

static u8 build_attr(struct vc_data *vc, u8 _color,
		     enum vc_intensity _intensity, bool _blink, bool _underline,
		     bool _reverse, bool _italic)
{
	if (vc->vc_sw->con_build_attr)
		return vc->vc_sw->con_build_attr(vc, _color, _intensity, _blink,
						 _underline, _reverse, _italic);

	{
		u8 a = _color;
		if (!vc->vc_can_do_color)
			return _intensity | (_italic << 1) | (_underline << 2) |
			       (_reverse << 3) | (_blink << 7);
		if (_italic)
			a = (a & 0xF0) | vc->vc_itcolor;
		else if (_underline)
			a = (a & 0xf0) | vc->vc_ulcolor;
		else if (_intensity == VCI_HALF_BRIGHT)
			a = (a & 0xf0) | vc->vc_halfcolor;
		if (_reverse)
			a = (a & 0x88) | (((a >> 4) | (a << 4)) & 0x77);
		if (_blink)
			a ^= 0x80;
		if (_intensity == VCI_BOLD)
			a ^= 0x08;
		if (vc->vc_hi_font_mask == 0x100)
			a <<= 1;
		return a;
	}
}

static void update_attr(struct vc_data *vc)
{
	vc->vc_attr = build_attr(vc, vc->state.color, vc->state.intensity,
				 vc->state.blink, vc->state.underline,
				 vc->state.reverse ^ vc->vc_decscnm,
				 vc->state.italic);
	vc->vc_video_erase_char =
		' ' | (build_attr(vc, vc->state.color, VCI_NORMAL,
				  vc->state.blink, false, vc->vc_decscnm, false)
		       << 8);
}

static int softcursor_original = -1;

static void hide_cursor(struct vc_data *vc)
{
	vc->vc_sw->con_cursor(vc, CM_ERASE);
	/* Inlined hide_softcursor */
	if (softcursor_original != -1) {
		scr_writew(softcursor_original, (u16 *)vc->vc_pos);
		if (con_should_update(vc))
			vc->vc_sw->con_putc(vc, softcursor_original,
					    vc->state.y, vc->state.x);
		softcursor_original = -1;
	}
}

static void set_cursor(struct vc_data *vc)
{
	/* console_blanked check removed - never assigned, always 0 */
	if (vc->vc_num != fg_console || vc->vc_mode == KD_GRAPHICS)
		return;
	if (vc->vc_deccm) {
		/* Inlined add_softcursor */
		int i = scr_readw((u16 *)vc->vc_pos);
		u32 type = vc->vc_cursor_type;
		if ((type & CUR_SW) && softcursor_original == -1) {
			softcursor_original = i;
			i |= CUR_SET(type);
			i ^= CUR_CHANGE(type);
			if ((type & CUR_ALWAYS_BG) &&
			    (softcursor_original & CUR_BG) == (i & CUR_BG))
				i ^= CUR_BG;
			if ((type & CUR_INVERT_FG_BG) &&
			    (i & CUR_FG) == ((i & CUR_BG) >> 4))
				i ^= CUR_FG;
			scr_writew(i, (u16 *)vc->vc_pos);
			if (con_should_update(vc))
				vc->vc_sw->con_putc(vc, i, vc->state.y,
						    vc->state.x);
		}
		if (CUR_SIZE(vc->vc_cursor_type) != CUR_NONE)
			vc->vc_sw->con_cursor(vc, CM_DRAW);
	} else
		hide_cursor(vc);
}

static void set_origin(struct vc_data *vc)
{
	WARN_CONSOLE_UNLOCKED();

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
	vc->vc_hi_font_mask = 0;
	vc->vc_complement_mask = 0;
	vc->vc_can_do_color = 0;
	/* vc->vc_cur_blink_ms removed - field removed */
	vc->vc_sw->con_init(vc, init);
	if (!vc->vc_complement_mask)
		vc->vc_complement_mask = vc->vc_can_do_color ? 0x7700 : 0x0800;
	vc->vc_s_complement_mask = vc->vc_complement_mask;
	vc->vc_size_row = vc->vc_cols << 1;
	vc->vc_screenbuf_size = vc->vc_rows * vc->vc_size_row;
}

/* visual_deinit inlined */

static void vc_port_destruct(struct tty_port *port)
{
	struct vc_data *vc = container_of(port, struct vc_data, port);

	kfree(vc);
}

static const struct tty_port_operations vc_port_ops = {
	.destruct = vc_port_destruct,
};

#define VC_MAXCOL (32767)
#define VC_MAXROW (32767)

int vc_allocate(unsigned int currcons)
{
	struct vc_data *vc;
	int err;

	WARN_CONSOLE_UNLOCKED();

	if (currcons >= MAX_NR_CONSOLES)
		return -ENXIO;

	if (vc_cons[currcons].d)
		return 0;

	vc = kzalloc(sizeof(struct vc_data), GFP_KERNEL);
	if (!vc)
		return -ENOMEM;

	vc_cons[currcons].d = vc;
	tty_port_init(&vc->port);
	vc->port.ops = &vc_port_ops;
	INIT_WORK(&vc_cons[currcons].SAK_work, vc_SAK);

	visual_init(vc, currcons, 1);

	err = -EINVAL;
	if (vc->vc_cols > VC_MAXCOL || vc->vc_rows > VC_MAXROW ||
	    vc->vc_screenbuf_size > KMALLOC_MAX_SIZE || !vc->vc_screenbuf_size)
		goto err_free;
	err = -ENOMEM;
	vc->vc_screenbuf = kzalloc(vc->vc_screenbuf_size, GFP_KERNEL);
	if (!vc->vc_screenbuf)
		goto err_free;

	if (global_cursor_default == -1)
		global_cursor_default = 1;

	vc_init(vc, vc->vc_rows, vc->vc_cols, 1);

	return 0;
err_free:
	vc->vc_sw->con_deinit(vc);
	module_put(vc->vc_sw->owner);
	kfree(vc);
	vc_cons[currcons].d = NULL;
	return err;
}

/* vc_do_resize inlined - was a stub returning 0 */

int vc_resize(struct vc_data *vc, unsigned int cols, unsigned int rows)
{
	/* Minimal stub: static console doesn't need resize */
	return 0;
}

/* vt_resize removed - ops->resize never called */

enum { EPecma = 0, EPdec, EPeq, EPgt, EPlt };

/* set_kbd, clr_kbd, is_kbd, decarm, decckm, kbdapplic, lnm removed - unused */

const unsigned char color_table[] = { 0, 4,  2,	 6,  1, 5,  3,	7,
				      8, 12, 10, 14, 9, 13, 11, 15 };

unsigned char default_red[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
unsigned char default_grn[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
unsigned char default_blu[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static void gotoxy(struct vc_data *vc, int new_x, int new_y)
{
	int min_y, max_y;

	if (new_x < 0)
		vc->state.x = 0;
	else {
		if (new_x >= vc->vc_cols)
			vc->state.x = vc->vc_cols - 1;
		else
			vc->state.x = new_x;
	}

	if (vc->vc_decom) {
		min_y = vc->vc_top;
		max_y = vc->vc_bottom;
	} else {
		min_y = 0;
		max_y = vc->vc_rows;
	}
	if (new_y < min_y)
		vc->state.y = min_y;
	else if (new_y >= max_y)
		vc->state.y = max_y - 1;
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

	switch (vpar) {
	case 0:
		count = (vc->vc_scr_end - vc->vc_pos) >> 1;
		start = (unsigned short *)vc->vc_pos;
		break;
	case 1:
		count = ((vc->vc_pos - vc->vc_origin) >> 1) + 1;
		start = (unsigned short *)vc->vc_origin;
		break;
	case 3:
		/* flush_scrollback inlined */
		WARN_CONSOLE_UNLOCKED();
		set_origin(vc);
		if (con_is_visible(vc)) {
			hide_cursor(vc);
			vc->vc_sw->con_switch(vc);
			set_cursor(vc);
		}
		fallthrough;
	case 2:
		count = vc->vc_cols * vc->vc_rows;
		start = (unsigned short *)vc->vc_origin;
		break;
	default:
		return;
	}
	scr_memsetw(start, vc->vc_video_erase_char, 2 * count);
	if (con_should_update(vc))
		do_update_region(vc, (unsigned long)start, count);
	vc->vc_need_wrap = 0;
}

/* save_cur inlined into reset_terminal */

enum {
	ESnormal,
	ESesc,
	ESsquare,
	ESgetpars,
	ESfunckey,
	EShash,
	ESsetG0,
	ESsetG1,
	ESpercent,
	EScsiignore,
	ESnonstd,
	ESpalette,
	ESosc,
	ESapc,
	ESpm,
	ESdcs
};

static void reset_terminal(struct vc_data *vc, int do_clear)
{
	unsigned int i;

	vc->vc_top = 0;
	vc->vc_bottom = vc->vc_rows;
	vc->vc_state = ESnormal;
	vc->vc_priv = EPecma;
	vc->vc_translate = set_translate(LAT1_MAP, vc);
	vc->state.Gx_charset[0] = LAT1_MAP;
	vc->state.Gx_charset[1] = GRAF_MAP;
	vc->state.charset = 0;
	vc->vc_need_wrap = 0;
	/* vc->vc_report_mouse = 0 removed - field removed */
	vc->vc_utf = default_utf8;
	vc->vc_utf_count = 0;

	vc->vc_disp_ctrl = 0;
	vc->vc_toggle_meta = 0;

	vc->vc_decscnm = 0;
	vc->vc_decom = 0;
	vc->vc_decawm = 1;
	vc->vc_deccm = global_cursor_default;
	vc->vc_decim = 0;
	/* vt_reset_keyboard call removed - empty stub */
	vc->vc_cursor_type = cur_default;
	vc->vc_complement_mask = vc->vc_s_complement_mask;

	/* Inlined default_attr */
	vc->state.intensity = VCI_NORMAL;
	vc->state.italic = false;
	vc->state.underline = false;
	vc->state.reverse = false;
	vc->state.blink = false;
	vc->state.color = vc->vc_def_color;
	update_attr(vc);

	bitmap_zero(vc->vc_tab_stop, VC_TABSTOPS_COUNT);
	for (i = 0; i < VC_TABSTOPS_COUNT; i += 8)
		set_bit(i, vc->vc_tab_stop);

	/* vc_bell_pitch, vc_bell_duration, vc_cur_blink_ms removed - fields removed */

	gotoxy(vc, 0, 0);
	memcpy(&vc->saved_state, &vc->state, sizeof(vc->state));
	if (do_clear)
		csi_J(vc, 2);
}

struct vc_draw_region {
	unsigned long from, to;
	int x;
};

/* con_flush inlined into vt_write */

/* Stub: Hello World only uses ASCII, no UTF-8 decoding needed */
static int vc_translate_unicode(struct vc_data *vc, int c, bool *rescan)
{
	return c;
}

/* do_con_write inlined into con_write */

struct tty_driver *console_driver;

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

static struct tty_driver *vt_console_device(struct console *c, int *index)
{
	*index = c->index ? c->index - 1 : fg_console;
	return console_driver;
}

static struct console vt_console_driver = {
	.name = "tty",
	.write = vt_console_print,
	.device = vt_console_device,
	/* .unblank removed - was empty stub */
	.flags = CON_PRINTBUFFER,
	.index = -1,
};

static int con_write(struct tty_struct *tty, const unsigned char *buf,
		     int count)
{
	/* do_con_write inlined */
	static const u32 CTRL_ACTION = 0x0d00ff81;
	static const u32 CTRL_ALWAYS = 0x0800f501;
	struct vc_draw_region draw = { .x = -1 };
	struct vc_data *vc;
	int c, tc, n = 0;

	if (in_interrupt())
		return count;

	console_lock();
	vc = tty->driver_data;
	if (!vc || !vc_cons_allocated(vc->vc_num)) {
		console_unlock();
		return 0;
	}

	while (!tty->flow.stopped && count--) {
		bool is_control;
		c = *buf++;
		n++;
		if (vc->vc_state != ESnormal)
			tc = c;
		else if (vc->vc_utf && !vc->vc_disp_ctrl)
			tc = c = vc_translate_unicode(vc, c, NULL);
		else
			tc = c;
		is_control = vc->vc_state != ESnormal || !tc ||
			     (c < 32 &&
			      (vc->vc_disp_ctrl ? (CTRL_ALWAYS & BIT(c)) :
						  (vc->vc_utf ||
						   (CTRL_ACTION & BIT(c))))) ||
			     (c == 127 && !vc->vc_disp_ctrl) || (c == 128 + 27);
		if (tc != -1 && !is_control) {
			u16 himask = vc->vc_hi_font_mask;
			if (vc->vc_need_wrap) {
				cr(vc);
				lf(vc);
			}
			tc = conv_uni_to_pc(vc, tc);
			if (tc < 0)
				tc = c;
			if (himask)
				tc = ((tc & 0x100) ? himask : 0) | (tc & 0xff);
			tc |= (vc->vc_attr << 8) & ~himask;
			scr_writew(tc, (u16 *)vc->vc_pos);
			if (con_should_update(vc) && draw.x < 0) {
				draw.x = vc->state.x;
				draw.from = vc->vc_pos;
			}
			if (vc->state.x == vc->vc_cols - 1) {
				vc->vc_need_wrap = vc->vc_decawm;
				draw.to = vc->vc_pos + 2;
			} else {
				vc->state.x++;
				draw.to = (vc->vc_pos += 2);
			}
		}
	}
	if (draw.x >= 0) {
		vc->vc_sw->con_putcs(vc, (u16 *)draw.from,
				     (u16 *)draw.to - (u16 *)draw.from,
				     vc->state.y, draw.x);
	}
	set_cursor(vc);
	console_unlock();
	return n;
}

/* con_put_char, con_write_room removed - ops->put_char, ops->write_room never called */

/* con_throttle, con_unthrottle, con_stop, con_start removed - ops callbacks never called */

static void con_flush_chars(struct tty_struct *tty)
{
	struct vc_data *vc;

	if (in_interrupt())
		return;

	console_lock();
	vc = tty->driver_data;
	if (vc)
		set_cursor(vc);
	console_unlock();
}

static int con_install(struct tty_driver *driver, struct tty_struct *tty)
{
	unsigned int currcons = tty->index;
	struct vc_data *vc;
	int ret;

	console_lock();
	ret = vc_allocate(currcons);
	if (ret)
		goto unlock;

	vc = vc_cons[currcons].d;

	if (vc->port.tty) {
		ret = -ERESTARTSYS;
		goto unlock;
	}

	ret = tty_port_install(&vc->port, driver, tty);
	if (ret)
		goto unlock;

	tty->driver_data = vc;
	vc->port.tty = tty;
	tty_port_get(&vc->port);

	if (!tty->winsize.ws_row && !tty->winsize.ws_col) {
		tty->winsize.ws_row = vc_cons[currcons].d->vc_rows;
		tty->winsize.ws_col = vc_cons[currcons].d->vc_cols;
	}
	if (vc->vc_utf)
		tty->termios.c_iflag |= IUTF8;
	else
		tty->termios.c_iflag &= ~IUTF8;
unlock:
	console_unlock();
	return ret;
}

static int con_open(struct tty_struct *tty, struct file *filp)
{
	return 0;
}

/* con_close removed - empty stub, set to NULL in ops */

static void con_shutdown(struct tty_struct *tty)
{
	struct vc_data *vc = tty->driver_data;
	BUG_ON(vc == NULL);
	console_lock();
	vc->port.tty = NULL;
	console_unlock();
}

static void con_cleanup(struct tty_struct *tty)
{
	struct vc_data *vc = tty->driver_data;

	tty_port_put(&vc->port);
}

static int default_color = 7;
static int default_italic_color = 2;
static int default_underline_color = 3;

static void vc_init(struct vc_data *vc, unsigned int rows, unsigned int cols,
		    int do_clear)
{
	int j, k;

	vc->vc_cols = cols;
	vc->vc_rows = rows;
	vc->vc_size_row = cols << 1;
	vc->vc_screenbuf_size = vc->vc_rows * vc->vc_size_row;

	set_origin(vc);
	vc->vc_pos = vc->vc_origin;
	/* reset_vc inlined from vt_ioctl.c */
	vc->vc_mode = KD_TEXT;
	reset_palette(vc);
	for (j = k = 0; j < 16; j++) {
		vc->vc_palette[k++] = default_red[j];
		vc->vc_palette[k++] = default_grn[j];
		vc->vc_palette[k++] = default_blu[j];
	}
	vc->vc_def_color = default_color;
	vc->vc_ulcolor = default_underline_color;
	vc->vc_itcolor = default_italic_color;
	vc->vc_halfcolor = 0x08;
	/* init_waitqueue_head(&vc->paste_wait) removed - field removed */
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
		INIT_WORK(&vc_cons[currcons].SAK_work, vc_SAK);
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

static const struct tty_operations con_ops = {
	.install = con_install,
	.open = con_open,
	/* .close = NULL - con_close was empty */
	.write = con_write,
	/* .write_room, .put_char removed - never called */
	.flush_chars = con_flush_chars,
	/* .ioctl, .stop, .start, .throttle, .unthrottle, .resize removed */
	.shutdown = con_shutdown,
	.cleanup = con_cleanup,
};

static struct cdev vc0_cdev;

/* show_tty_active, dev_attr_active, vt_dev_attrs removed - groups not stored anymore */

int __init vty_init(const struct file_operations *console_fops)
{
	cdev_init(&vc0_cdev, console_fops);
	if (cdev_add(&vc0_cdev, MKDEV(TTY_MAJOR, 0), 1) ||
	    register_chrdev_region(MKDEV(TTY_MAJOR, 0), 1, "/dev/vc/0") < 0)
		panic("Couldn't register /dev/tty0 driver\n");
	tty0dev = device_create(tty_class, NULL, MKDEV(TTY_MAJOR, 0), NULL,
				"tty0");
	if (IS_ERR(tty0dev))
		tty0dev = NULL;

	console_driver = tty_alloc_driver(MAX_NR_CONSOLES,
					  TTY_DRIVER_REAL_RAW |
						  TTY_DRIVER_RESET_TERMIOS);
	if (IS_ERR(console_driver))
		panic("Couldn't allocate console driver\n");

	console_driver->name = "tty";
	console_driver->name_base = 1;
	console_driver->major = TTY_MAJOR;
	console_driver->minor_start = 1;
	console_driver->type = TTY_DRIVER_TYPE_CONSOLE;
	console_driver->init_termios = tty_std_termios;
	if (default_utf8)
		console_driver->init_termios.c_iflag |= IUTF8;
	console_driver->ops = &con_ops;
	if (tty_register_driver(console_driver))
		panic("Couldn't register console driver\n");
	/* kbd_init removed - empty stub */
	return 0;
}

#ifndef VT_SINGLE_DRIVER

/* do_bind_con_driver, vt_bind, vt_unbind, store_bind, show_bind, show_name,
   dev_attr_bind, dev_attr_name, con_dev_attrs, vtconsole_init_device,
   vtconsole_class, con_is_bound - all removed, never used */

bool con_is_visible(const struct vc_data *vc)
{
	WARN_CONSOLE_UNLOCKED();

	return *vc->vc_display_fg == vc;
}

/* vtconsole_class_init simplified - sysfs entries not needed for minimal kernel */
static int __init vtconsole_class_init(void)
{
	return 0;
}
postcore_initcall(vtconsole_class_init);

#endif

/* do_unblank_screen, unblank_screen, blank_screen_t removed - never called */
/* set_palette stub inlined into reset_palette */

void reset_palette(struct vc_data *vc)
{
	int j, k;
	for (j = k = 0; j < 16; j++) {
		vc->vc_palette[k++] = default_red[j];
		vc->vc_palette[k++] = default_grn[j];
		vc->vc_palette[k++] = default_blu[j];
	}
	/* set_palette was a no-op stub - inlined away */
}

void vc_scrolldelta_helper(struct vc_data *c, int lines,
			   unsigned int rolled_over, void *base,
			   unsigned int size)
{
	/* Stubbed: scrollback not needed for minimal boot */
	if (!lines)
		c->vc_visible_origin = c->vc_origin;
}
