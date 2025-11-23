 

#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched/signal.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
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
#include <linux/selection.h>
#include <linux/tiocl.h>
#include <linux/kbd_kern.h>
#include <linux/consolemap.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/pm.h>
#include <linux/font.h>
#include <linux/bitops.h>
#include <linux/notifier.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#include <linux/ctype.h>
#include <linux/bsearch.h>
#include <linux/gcd.h>

#define MAX_NR_CON_DRIVER 16

#define CON_DRIVER_FLAG_MODULE 1
#define CON_DRIVER_FLAG_INIT   2
#define CON_DRIVER_FLAG_ATTR   4
#define CON_DRIVER_FLAG_ZOMBIE 8

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

#define DEFAULT_BELL_PITCH	750
#define DEFAULT_BELL_DURATION	(HZ/8)
#define DEFAULT_CURSOR_BLINK_MS	200

struct vc vc_cons [MAX_NR_CONSOLES];

#ifndef VT_SINGLE_DRIVER
static const struct consw *con_driver_map[MAX_NR_CONSOLES];
#endif

static int con_open(struct tty_struct *, struct file *);
static void vc_init(struct vc_data *vc, unsigned int rows,
		    unsigned int cols, int do_clear);
static void gotoxy(struct vc_data *vc, int new_x, int new_y);
static void save_cur(struct vc_data *vc);
static void reset_terminal(struct vc_data *vc, int do_clear);
static void con_flush_chars(struct tty_struct *tty);
static void set_cursor(struct vc_data *vc);
static void hide_cursor(struct vc_data *vc);
static void console_callback(struct work_struct *ignored);
static void con_driver_unregister_callback(struct work_struct *ignored);
static void blank_screen_t(struct timer_list *unused);
static void set_palette(struct vc_data *vc);

#define vt_get_kmsg_redirect() vt_kmsg_redirect(-1)

static int printable;		
int default_utf8 = true;
module_param(default_utf8, int, S_IRUGO | S_IWUSR);
int global_cursor_default = -1;
module_param(global_cursor_default, int, S_IRUGO | S_IWUSR);

static int cur_default = CUR_UNDERLINE;
module_param(cur_default, int, S_IRUGO | S_IWUSR);

int do_poke_blanked_console;
int console_blanked;

static int blankinterval;
core_param(consoleblank, blankinterval, int, 0444);

static DECLARE_WORK(console_work, console_callback);
static DECLARE_WORK(con_driver_unregister_work, con_driver_unregister_callback);

int fg_console;
int last_console;
int want_console = -1;

static struct vc_data *master_display_fg;

int (*console_blank_hook)(int);

static DEFINE_TIMER(console_timer, blank_screen_t);
static int blank_state;
enum {
	blank_off = 0,
	blank_normal_wait,
	blank_vesa_wait,
};

static struct device *tty0dev;

static ATOMIC_NOTIFIER_HEAD(vt_notifier_list);

int register_vt_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&vt_notifier_list, nb);
}

int unregister_vt_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&vt_notifier_list, nb);
}

static void notify_write(struct vc_data *vc, unsigned int unicode)
{
	/* Stub: skip VT notifications for minimal kernel */
}

static void notify_update(struct vc_data *vc)
{
	/* Stub: skip VT notifications for minimal kernel */
}

static inline bool con_is_fg(const struct vc_data *vc)
{
	return vc->vc_num == fg_console;
}

static inline bool con_should_update(const struct vc_data *vc)
{
	return con_is_visible(vc) && !console_blanked;
}

static inline unsigned short *screenpos(const struct vc_data *vc, int offset,
		bool viewed)
{
	unsigned short *p;
	
	if (!viewed)
		p = (unsigned short *)(vc->vc_origin + offset);
	else if (!vc->vc_sw->con_screen_pos)
		p = (unsigned short *)(vc->vc_visible_origin + offset);
	else
		p = vc->vc_sw->con_screen_pos(vc, offset);
	return p;
}

void schedule_console_callback(void)
{
	schedule_work(&console_work);
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

static void vc_uniscr_free(struct uni_screen *uniscr)
{
	vfree(uniscr);
}

static void vc_uniscr_set(struct vc_data *vc, struct uni_screen *new_uniscr)
{
	vc_uniscr_free(vc->vc_uni_screen);
	vc->vc_uni_screen = new_uniscr;
}

static void vc_uniscr_clear_line(struct vc_data *vc, unsigned int x,
				 unsigned int nr)
{
	/* Stub: skip unicode screen buffer clear for minimal kernel */
}

static void vc_uniscr_clear_lines(struct vc_data *vc, unsigned int y,
				  unsigned int nr)
{
	/* Stub: skip unicode screen buffer clear for minimal kernel */
}

static void vc_uniscr_scroll(struct vc_data *vc, unsigned int t, unsigned int b,
			     enum con_scroll dir, unsigned int nr)
{
	/* Stub: skip unicode screen buffer scroll for minimal kernel */
}

int vc_uniscr_check(struct vc_data *vc)
{
	/* Stub: unicode screen not needed for minimal kernel */
	return -ENOMEM;
}

void vc_uniscr_copy_line(const struct vc_data *vc, void *dest, bool viewed,
			 unsigned int row, unsigned int col, unsigned int nr)
{
	/* Stub: unicode screen copy not needed for minimal kernel */
}

static void con_scroll(struct vc_data *vc, unsigned int t, unsigned int b,
		enum con_scroll dir, unsigned int nr)
{
	u16 *clear, *d, *s;

	if (t + nr >= b)
		nr = b - t - 1;
	if (b > vc->vc_rows || t >= b || nr < 1)
		return;
	vc_uniscr_scroll(vc, t, b, dir, nr);
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

	p = (u16 *) start;
	if (!vc->vc_sw->con_getxy) {
		offset = (start - vc->vc_origin) / 2;
		xx = offset % vc->vc_cols;
		yy = offset / vc->vc_cols;
	} else {
		int nxx, nyy;
		start = vc->vc_sw->con_getxy(vc, start, &nxx, &nyy);
		xx = nxx; yy = nyy;
	}
	for(;;) {
		u16 attrib = scr_readw(p) & 0xff00;
		int startx = xx;
		u16 *q = p;
		while (xx < vc->vc_cols && count) {
			if (attrib != (scr_readw(p) & 0xff00)) {
				if (p > q)
					vc->vc_sw->con_putcs(vc, q, p-q, yy, startx);
				startx = xx;
				q = p;
				attrib = scr_readw(p) & 0xff00;
			}
			p++;
			xx++;
			count--;
		}
		if (p > q)
			vc->vc_sw->con_putcs(vc, q, p-q, yy, startx);
		if (!count)
			break;
		xx = 0;
		yy++;
		if (vc->vc_sw->con_getxy) {
			p = (u16 *)start;
			start = vc->vc_sw->con_getxy(vc, start, NULL, NULL);
		}
	}
}

void update_region(struct vc_data *vc, unsigned long start, int count)
{
	WARN_CONSOLE_UNLOCKED();

	if (con_should_update(vc)) {
		hide_cursor(vc);
		do_update_region(vc, start, count);
		set_cursor(vc);
	}
}

static u8 build_attr(struct vc_data *vc, u8 _color,
		enum vc_intensity _intensity, bool _blink, bool _underline,
		bool _reverse, bool _italic)
{
	if (vc->vc_sw->con_build_attr)
		return vc->vc_sw->con_build_attr(vc, _color, _intensity,
		       _blink, _underline, _reverse, _italic);

	{
	u8 a = _color;
	if (!vc->vc_can_do_color)
		return _intensity |
		       (_italic    << 1) |
		       (_underline << 2) |
		       (_reverse   << 3) |
		       (_blink     << 7);
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
	              vc->state.reverse ^ vc->vc_decscnm, vc->state.italic);
	vc->vc_video_erase_char = ' ' | (build_attr(vc, vc->state.color,
				VCI_NORMAL, vc->state.blink, false,
				vc->vc_decscnm, false) << 8);
}

void invert_screen(struct vc_data *vc, int offset, int count, bool viewed)
{
	
}

void complement_pos(struct vc_data *vc, int offset)
{
}

static int softcursor_original = -1;

static void add_softcursor(struct vc_data *vc)
{
	int i = scr_readw((u16 *) vc->vc_pos);
	u32 type = vc->vc_cursor_type;

	if (!(type & CUR_SW))
		return;
	if (softcursor_original != -1)
		return;
	softcursor_original = i;
	i |= CUR_SET(type);
	i ^= CUR_CHANGE(type);
	if ((type & CUR_ALWAYS_BG) &&
			(softcursor_original & CUR_BG) == (i & CUR_BG))
		i ^= CUR_BG;
	if ((type & CUR_INVERT_FG_BG) && (i & CUR_FG) == ((i & CUR_BG) >> 4))
		i ^= CUR_FG;
	scr_writew(i, (u16 *)vc->vc_pos);
	if (con_should_update(vc))
		vc->vc_sw->con_putc(vc, i, vc->state.y, vc->state.x);
}

static void hide_softcursor(struct vc_data *vc)
{
	if (softcursor_original != -1) {
		scr_writew(softcursor_original, (u16 *)vc->vc_pos);
		if (con_should_update(vc))
			vc->vc_sw->con_putc(vc, softcursor_original,
					vc->state.y, vc->state.x);
		softcursor_original = -1;
	}
}

static void hide_cursor(struct vc_data *vc)
{
	if (vc_is_sel(vc))
		clear_selection();

	vc->vc_sw->con_cursor(vc, CM_ERASE);
	hide_softcursor(vc);
}

static void set_cursor(struct vc_data *vc)
{
	if (!con_is_fg(vc) || console_blanked || vc->vc_mode == KD_GRAPHICS)
		return;
	if (vc->vc_deccm) {
		if (vc_is_sel(vc))
			clear_selection();
		add_softcursor(vc);
		if (CUR_SIZE(vc->vc_cursor_type) != CUR_NONE)
			vc->vc_sw->con_cursor(vc, CM_DRAW);
	} else
		hide_cursor(vc);
}

static void set_origin(struct vc_data *vc)
{
	WARN_CONSOLE_UNLOCKED();

	if (!con_is_visible(vc) ||
	    !vc->vc_sw->con_set_origin ||
	    !vc->vc_sw->con_set_origin(vc))
		vc->vc_origin = (unsigned long)vc->vc_screenbuf;
	vc->vc_visible_origin = vc->vc_origin;
	vc->vc_scr_end = vc->vc_origin + vc->vc_screenbuf_size;
	vc->vc_pos = vc->vc_origin + vc->vc_size_row * vc->state.y +
		2 * vc->state.x;
}

static void save_screen(struct vc_data *vc)
{
	WARN_CONSOLE_UNLOCKED();

	if (vc->vc_sw->con_save_screen)
		vc->vc_sw->con_save_screen(vc);
}

static void flush_scrollback(struct vc_data *vc)
{
	WARN_CONSOLE_UNLOCKED();

	set_origin(vc);
	if (vc->vc_sw->con_flush_scrollback) {
		vc->vc_sw->con_flush_scrollback(vc);
	} else if (con_is_visible(vc)) {
		
		hide_cursor(vc);
		vc->vc_sw->con_switch(vc);
		set_cursor(vc);
	}
}

void clear_buffer_attributes(struct vc_data *vc)
{
}

void redraw_screen(struct vc_data *vc, int is_switch)
{
	int redraw = 0;

	WARN_CONSOLE_UNLOCKED();

	if (!vc) {
		
		
		return;
	}

	if (is_switch) {
		struct vc_data *old_vc = vc_cons[fg_console].d;
		if (old_vc == vc)
			return;
		if (!con_is_visible(vc))
			redraw = 1;
		*vc->vc_display_fg = vc;
		fg_console = vc->vc_num;
		hide_cursor(old_vc);
		if (!con_is_visible(old_vc)) {
			save_screen(old_vc);
			set_origin(old_vc);
		}
		if (tty0dev)
			sysfs_notify(&tty0dev->kobj, NULL, "active");
	} else {
		hide_cursor(vc);
		redraw = 1;
	}

	if (redraw) {
		int update;
		int old_was_color = vc->vc_can_do_color;

		set_origin(vc);
		update = vc->vc_sw->con_switch(vc);
		set_palette(vc);
		
		if (old_was_color != vc->vc_can_do_color) {
			update_attr(vc);
			clear_buffer_attributes(vc);
		}

		if (update && vc->vc_mode != KD_GRAPHICS)
			do_update_region(vc, vc->vc_origin, vc->vc_screenbuf_size / 2);
	}
	set_cursor(vc);
	if (is_switch) {
		vt_set_leds_compute_shiftstate();
		notify_update(vc);
	}
}

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
	if (vc->vc_uni_pagedir_loc)
		con_free_unimap(vc);
	vc->vc_uni_pagedir_loc = &vc->vc_uni_pagedir;
	vc->vc_uni_pagedir = NULL;
	vc->vc_hi_font_mask = 0;
	vc->vc_complement_mask = 0;
	vc->vc_can_do_color = 0;
	vc->vc_cur_blink_ms = DEFAULT_CURSOR_BLINK_MS;
	vc->vc_sw->con_init(vc, init);
	if (!vc->vc_complement_mask)
		vc->vc_complement_mask = vc->vc_can_do_color ? 0x7700 : 0x0800;
	vc->vc_s_complement_mask = vc->vc_complement_mask;
	vc->vc_size_row = vc->vc_cols << 1;
	vc->vc_screenbuf_size = vc->vc_rows * vc->vc_size_row;
}

static void visual_deinit(struct vc_data *vc)
{
	vc->vc_sw->con_deinit(vc);
	module_put(vc->vc_sw->owner);
}

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
	struct vt_notifier_param param;
	struct vc_data *vc;
	int err;

	WARN_CONSOLE_UNLOCKED();

	if (currcons >= MAX_NR_CONSOLES)
		return -ENXIO;

	if (vc_cons[currcons].d)
		return 0;

	
	
	
	param.vc = vc = kzalloc(sizeof(struct vc_data), GFP_KERNEL);
	if (!vc)
		return -ENOMEM;

	vc_cons[currcons].d = vc;
	tty_port_init(&vc->port);
	vc->port.ops = &vc_port_ops;
	INIT_WORK(&vc_cons[currcons].SAK_work, vc_SAK);

	visual_init(vc, currcons, 1);

	if (!*vc->vc_uni_pagedir_loc)
		con_set_default_unimap(vc);

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
	vcs_make_sysfs(currcons);
	atomic_notifier_call_chain(&vt_notifier_list, VT_ALLOCATE, &param);

	return 0;
err_free:
	visual_deinit(vc);
	kfree(vc);
	vc_cons[currcons].d = NULL;
	return err;
}

static int vc_do_resize(struct tty_struct *tty, struct vc_data *vc,
				unsigned int cols, unsigned int lines)
{
	/* Minimal stub: static console doesn't need resize */
	return 0;
}

int vc_resize(struct vc_data *vc, unsigned int cols, unsigned int rows)
{
	return vc_do_resize(vc->port.tty, vc, cols, rows);
}

static int vt_resize(struct tty_struct *tty, struct winsize *ws)
{
	struct vc_data *vc = tty->driver_data;
	int ret;

	console_lock();
	ret = vc_do_resize(tty, vc, ws->ws_col, ws->ws_row);
	console_unlock();
	return ret;
}

struct vc_data *vc_deallocate(unsigned int currcons)
{
	struct vc_data *vc = NULL;

	WARN_CONSOLE_UNLOCKED();

	if (vc_cons_allocated(currcons)) {
		struct vt_notifier_param param;

		param.vc = vc = vc_cons[currcons].d;
		atomic_notifier_call_chain(&vt_notifier_list, VT_DEALLOCATE, &param);
		vcs_remove_sysfs(currcons);
		visual_deinit(vc);
		con_free_unimap(vc);
		put_pid(vc->vt_pid);
		vc_uniscr_set(vc, NULL);
		kfree(vc->vc_screenbuf);
		vc_cons[currcons].d = NULL;
	}
	return vc;
}

enum { EPecma = 0, EPdec, EPeq, EPgt, EPlt};

#define set_kbd(vc, x)	vt_set_kbd_mode_bit((vc)->vc_num, (x))
#define clr_kbd(vc, x)	vt_clr_kbd_mode_bit((vc)->vc_num, (x))
#define is_kbd(vc, x)	vt_get_kbd_mode_bit((vc)->vc_num, (x))

#define decarm		VC_REPEAT
#define decckm		VC_CKMODE
#define kbdapplic	VC_APPLIC
#define lnm		VC_CRLF

const unsigned char color_table[] = { 0, 4, 2, 6, 1, 5, 3, 7,
				       8,12,10,14, 9,13,11,15 };

unsigned char default_red[] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};
module_param_array(default_red, byte, NULL, S_IRUGO | S_IWUSR);

unsigned char default_grn[] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};
module_param_array(default_grn, byte, NULL, S_IRUGO | S_IWUSR);

unsigned char default_blu[] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};
module_param_array(default_blu, byte, NULL, S_IRUGO | S_IWUSR);

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

void scrollback(struct vc_data *vc)
{
}

void scrollfront(struct vc_data *vc, int lines)
{
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
	notify_write(vc, '\n');
}

static inline void cr(struct vc_data *vc)
{
	vc->vc_pos -= vc->state.x << 1;
	vc->vc_need_wrap = vc->state.x = 0;
	notify_write(vc, '\r');
}

static inline void bs(struct vc_data *vc)
{
	if (vc->state.x) {
		vc->vc_pos -= 2;
		vc->state.x--;
		vc->vc_need_wrap = 0;
		notify_write(vc, '\b');
	}
}


static void csi_J(struct vc_data *vc, int vpar)
{
	unsigned int count;
	unsigned short * start;

	switch (vpar) {
		case 0:	
			vc_uniscr_clear_line(vc, vc->state.x,
					     vc->vc_cols - vc->state.x);
			vc_uniscr_clear_lines(vc, vc->state.y + 1,
					      vc->vc_rows - vc->state.y - 1);
			count = (vc->vc_scr_end - vc->vc_pos) >> 1;
			start = (unsigned short *)vc->vc_pos;
			break;
		case 1:	
			vc_uniscr_clear_line(vc, 0, vc->state.x + 1);
			vc_uniscr_clear_lines(vc, 0, vc->state.y);
			count = ((vc->vc_pos - vc->vc_origin) >> 1) + 1;
			start = (unsigned short *)vc->vc_origin;
			break;
		case 3: 
			flush_scrollback(vc);
			fallthrough;
		case 2: 
			vc_uniscr_clear_lines(vc, 0, vc->vc_rows);
			count = vc->vc_cols * vc->vc_rows;
			start = (unsigned short *)vc->vc_origin;
			break;
		default:
			return;
	}
	scr_memsetw(start, vc->vc_video_erase_char, 2 * count);
	if (con_should_update(vc))
		do_update_region(vc, (unsigned long) start, count);
	vc->vc_need_wrap = 0;
}

static void default_attr(struct vc_data *vc)
{
	vc->state.intensity = VCI_NORMAL;
	vc->state.italic = false;
	vc->state.underline = false;
	vc->state.reverse = false;
	vc->state.blink = false;
	vc->state.color = vc->vc_def_color;
}

void mouse_report(struct tty_struct *tty, int butt, int mrx, int mry)
{
}

int mouse_reporting(void)
{
	return 0;
}

static void save_cur(struct vc_data *vc)
{
	memcpy(&vc->saved_state, &vc->state, sizeof(vc->state));
}

enum { ESnormal, ESesc, ESsquare, ESgetpars, ESfunckey,
	EShash, ESsetG0, ESsetG1, ESpercent, EScsiignore, ESnonstd,
	ESpalette, ESosc, ESapc, ESpm, ESdcs };

static void reset_terminal(struct vc_data *vc, int do_clear)
{
	unsigned int i;

	vc->vc_top		= 0;
	vc->vc_bottom		= vc->vc_rows;
	vc->vc_state		= ESnormal;
	vc->vc_priv		= EPecma;
	vc->vc_translate	= set_translate(LAT1_MAP, vc);
	vc->state.Gx_charset[0]	= LAT1_MAP;
	vc->state.Gx_charset[1]	= GRAF_MAP;
	vc->state.charset	= 0;
	vc->vc_need_wrap	= 0;
	vc->vc_report_mouse	= 0;
	vc->vc_utf              = default_utf8;
	vc->vc_utf_count	= 0;

	vc->vc_disp_ctrl	= 0;
	vc->vc_toggle_meta	= 0;

	vc->vc_decscnm		= 0;
	vc->vc_decom		= 0;
	vc->vc_decawm		= 1;
	vc->vc_deccm		= global_cursor_default;
	vc->vc_decim		= 0;

	vt_reset_keyboard(vc->vc_num);

	vc->vc_cursor_type = cur_default;
	vc->vc_complement_mask = vc->vc_s_complement_mask;

	default_attr(vc);
	update_attr(vc);

	bitmap_zero(vc->vc_tab_stop, VC_TABSTOPS_COUNT);
	for (i = 0; i < VC_TABSTOPS_COUNT; i += 8)
		set_bit(i, vc->vc_tab_stop);

	vc->vc_bell_pitch = DEFAULT_BELL_PITCH;
	vc->vc_bell_duration = DEFAULT_BELL_DURATION;
	vc->vc_cur_blink_ms = DEFAULT_CURSOR_BLINK_MS;

	gotoxy(vc, 0, 0);
	save_cur(vc);
	if (do_clear)
	    csi_J(vc, 2);
}

struct vc_draw_region {
	unsigned long from, to;
	int x;
};

static void con_flush(struct vc_data *vc, struct vc_draw_region *draw)
{
	if (draw->x < 0)
		return;

	vc->vc_sw->con_putcs(vc, (u16 *)draw->from,
			(u16 *)draw->to - (u16 *)draw->from, vc->state.y,
			draw->x);
	draw->x = -1;
}

static inline int vc_translate_ascii(const struct vc_data *vc, int c)
{
	if (IS_ENABLED(CONFIG_CONSOLE_TRANSLATIONS)) {
		if (vc->vc_toggle_meta)
			c |= 0x80;

		return vc->vc_translate[c];
	}

	return c;
}

static inline int vc_sanitize_unicode(const int c)
{
	if ((c >= 0xd800 && c <= 0xdfff) || c == 0xfffe || c == 0xffff)
		return 0xfffd;

	return c;
}

static int vc_translate_unicode(struct vc_data *vc, int c, bool *rescan)
{
	static const u32 utf8_length_changes[] = {
		0x0000007f, 0x000007ff, 0x0000ffff,
		0x001fffff, 0x03ffffff, 0x7fffffff
	};

	
	if ((c & 0xc0) == 0x80) {
		
		if (!vc->vc_utf_count)
			return 0xfffd;

		vc->vc_utf_char = (vc->vc_utf_char << 6) | (c & 0x3f);
		vc->vc_npar++;
		if (--vc->vc_utf_count)
			goto need_more_bytes;

		
		c = vc->vc_utf_char;
		
		if (c <= utf8_length_changes[vc->vc_npar - 1] ||
				c > utf8_length_changes[vc->vc_npar])
			return 0xfffd;

		return vc_sanitize_unicode(c);
	}

	
	if (vc->vc_utf_count) {
		
		*rescan = true;
		vc->vc_utf_count = 0;
		return 0xfffd;
	}

	
	if (c <= 0x7f)
		return c;

	
	vc->vc_npar = 0;
	if ((c & 0xe0) == 0xc0) {
		vc->vc_utf_count = 1;
		vc->vc_utf_char = (c & 0x1f);
	} else if ((c & 0xf0) == 0xe0) {
		vc->vc_utf_count = 2;
		vc->vc_utf_char = (c & 0x0f);
	} else if ((c & 0xf8) == 0xf0) {
		vc->vc_utf_count = 3;
		vc->vc_utf_char = (c & 0x07);
	} else if ((c & 0xfc) == 0xf8) {
		vc->vc_utf_count = 4;
		vc->vc_utf_char = (c & 0x03);
	} else if ((c & 0xfe) == 0xfc) {
		vc->vc_utf_count = 5;
		vc->vc_utf_char = (c & 0x01);
	} else {
		
		return 0xfffd;
	}

need_more_bytes:
	return -1;
}

static int vc_translate(struct vc_data *vc, int *c, bool *rescan)
{
	
	if (vc->vc_state != ESnormal)
		return *c;

	if (vc->vc_utf && !vc->vc_disp_ctrl)
		return *c = vc_translate_unicode(vc, *c, rescan);

	
	return vc_translate_ascii(vc, *c);
}

static inline unsigned char vc_invert_attr(const struct vc_data *vc)
{
	if (!vc->vc_can_do_color)
		return vc->vc_attr ^ 0x08;

	if (vc->vc_hi_font_mask == 0x100)
		return   (vc->vc_attr & 0x11) |
			((vc->vc_attr & 0xe0) >> 4) |
			((vc->vc_attr & 0x0e) << 4);

	return   (vc->vc_attr & 0x88) |
		((vc->vc_attr & 0x70) >> 4) |
		((vc->vc_attr & 0x07) << 4);
}

static bool vc_is_control(struct vc_data *vc, int tc, int c)
{
	
	static const u32 CTRL_ACTION = 0x0d00ff81;
	
	static const u32 CTRL_ALWAYS = 0x0800f501;

	if (vc->vc_state != ESnormal)
		return true;

	if (!tc)
		return true;

	
	if (c < 32) {
		if (vc->vc_disp_ctrl)
			return CTRL_ALWAYS & BIT(c);
		else
			return vc->vc_utf || (CTRL_ACTION & BIT(c));
	}

	if (c == 127 && !vc->vc_disp_ctrl)
		return true;

	if (c == 128 + 27)
		return true;

	return false;
}

static int vc_con_write_normal(struct vc_data *vc, int tc, int c,
		struct vc_draw_region *draw)
{
	/* Minimal stub: simplified character output without UTF-8/double-width support */
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

	if (con_should_update(vc) && draw->x < 0) {
		draw->x = vc->state.x;
		draw->from = vc->vc_pos;
	}

	if (vc->state.x == vc->vc_cols - 1) {
		vc->vc_need_wrap = vc->vc_decawm;
		draw->to = vc->vc_pos + 2;
	} else {
		vc->state.x++;
		draw->to = (vc->vc_pos += 2);
	}

	notify_write(vc, c);
	return 0;
}

static int do_con_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
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
		c = *buf++;
		n++;
		tc = vc_translate(vc, &c, NULL);
		if (tc != -1 && !vc_is_control(vc, tc, c))
			vc_con_write_normal(vc, tc, c, &draw);
	}
	con_flush(vc, &draw);
	console_unlock();
	return n;
}

static void console_callback(struct work_struct *ignored)
{
	/* Stub: minimal console callback for simple kernel */
	console_lock();
	console_unlock();
}

int set_console(int nr)
{
	/* Stub: no console switching in minimal kernel */
	return -EINVAL;
}

struct tty_driver *console_driver;

int vt_kmsg_redirect(int new)
{
	/* Stub: no kmsg redirection */
	return 0;
}

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
			scr_writew((vc->vc_attr << 8) + c, (unsigned short *)vc->vc_pos);
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
	*index = c->index ? c->index-1 : fg_console;
	return console_driver;
}

static struct console vt_console_driver = {
	.name		= "tty",
	.write		= vt_console_print,
	.device		= vt_console_device,
	.unblank	= unblank_screen,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

int tioclinux(struct tty_struct *tty, unsigned long arg)
{
	return -EINVAL;
}

static int con_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	int	retval;

	retval = do_con_write(tty, buf, count);
	con_flush_chars(tty);

	return retval;
}

static int con_put_char(struct tty_struct *tty, unsigned char ch)
{
	return do_con_write(tty, &ch, 1);
}

static unsigned int con_write_room(struct tty_struct *tty)
{
	if (tty->flow.stopped)
		return 0;
	return 32768;		
}

static void con_throttle(struct tty_struct *tty)
{
}

static void con_unthrottle(struct tty_struct *tty)
{
	struct vc_data *vc = tty->driver_data;

	wake_up_interruptible(&vc->paste_wait);
}

static void con_stop(struct tty_struct *tty)
{
	int console_num;
	if (!tty)
		return;
	console_num = tty->index;
	if (!vc_cons_allocated(console_num))
		return;
	vt_kbd_con_stop(console_num);
}

static void con_start(struct tty_struct *tty)
{
	int console_num;
	if (!tty)
		return;
	console_num = tty->index;
	if (!vc_cons_allocated(console_num))
		return;
	vt_kbd_con_start(console_num);
}

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

static void con_close(struct tty_struct *tty, struct file *filp)
{
	
}

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

static int default_color           = 7; 
static int default_italic_color    = 2;  
static int default_underline_color = 3;  
module_param_named(color, default_color, int, S_IRUGO | S_IWUSR);
module_param_named(italic, default_italic_color, int, S_IRUGO | S_IWUSR);
module_param_named(underline, default_underline_color, int, S_IRUGO | S_IWUSR);

static void vc_init(struct vc_data *vc, unsigned int rows,
		    unsigned int cols, int do_clear)
{
	int j, k ;

	vc->vc_cols = cols;
	vc->vc_rows = rows;
	vc->vc_size_row = cols << 1;
	vc->vc_screenbuf_size = vc->vc_rows * vc->vc_size_row;

	set_origin(vc);
	vc->vc_pos = vc->vc_origin;
	reset_vc(vc);
	for (j=k=0; j<16; j++) {
		vc->vc_palette[k++] = default_red[j] ;
		vc->vc_palette[k++] = default_grn[j] ;
		vc->vc_palette[k++] = default_blu[j] ;
	}
	vc->vc_def_color       = default_color;
	vc->vc_ulcolor         = default_underline_color;
	vc->vc_itcolor         = default_italic_color;
	vc->vc_halfcolor       = 0x08;   
	init_waitqueue_head(&vc->paste_wait);
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

	if (blankinterval) {
		blank_state = blank_normal_wait;
		mod_timer(&console_timer, jiffies + (blankinterval * HZ));
	}

	for (currcons = 0; currcons < MIN_NR_CONSOLES; currcons++) {
		vc_cons[currcons].d = vc = kzalloc(sizeof(struct vc_data), GFP_NOWAIT);
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
	save_screen(vc);
	gotoxy(vc, vc->state.x, vc->state.y);
	csi_J(vc, 0);
	update_screen(vc);
	printable = 1;

	console_unlock();

	register_console(&vt_console_driver);
	return 0;
}
console_initcall(con_init);

static const struct tty_operations con_ops = {
	.install = con_install,
	.open = con_open,
	.close = con_close,
	.write = con_write,
	.write_room = con_write_room,
	.put_char = con_put_char,
	.flush_chars = con_flush_chars,
	.ioctl = vt_ioctl,
	.stop = con_stop,
	.start = con_start,
	.throttle = con_throttle,
	.unthrottle = con_unthrottle,
	.resize = vt_resize,
	.shutdown = con_shutdown,
	.cleanup = con_cleanup,
};

static struct cdev vc0_cdev;

static ssize_t show_tty_active(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "tty%d\n", fg_console + 1);
}
static DEVICE_ATTR(active, S_IRUGO, show_tty_active, NULL);

static struct attribute *vt_dev_attrs[] = {
	&dev_attr_active.attr,
	NULL
};

ATTRIBUTE_GROUPS(vt_dev);

int __init vty_init(const struct file_operations *console_fops)
{
	cdev_init(&vc0_cdev, console_fops);
	if (cdev_add(&vc0_cdev, MKDEV(TTY_MAJOR, 0), 1) ||
	    register_chrdev_region(MKDEV(TTY_MAJOR, 0), 1, "/dev/vc/0") < 0)
		panic("Couldn't register /dev/tty0 driver\n");
	tty0dev = device_create_with_groups(tty_class, NULL,
					    MKDEV(TTY_MAJOR, 0), NULL,
					    vt_dev_groups, "tty0");
	if (IS_ERR(tty0dev))
		tty0dev = NULL;

	vcs_init();

	console_driver = tty_alloc_driver(MAX_NR_CONSOLES, TTY_DRIVER_REAL_RAW |
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
	tty_set_operations(console_driver, &con_ops);
	if (tty_register_driver(console_driver))
		panic("Couldn't register console driver\n");
	kbd_init();
	console_map_init();
	return 0;
}

#ifndef VT_SINGLE_DRIVER

static struct class *vtconsole_class;

static int do_bind_con_driver(const struct consw *csw, int first, int last,
			   int deflt)
{
	struct module *owner = csw->owner;
	const char *desc = NULL;
	struct con_driver *con_driver;
	int i, j = -1, k = -1, retval = -ENODEV;

	if (!try_module_get(owner))
		return -ENODEV;

	WARN_CONSOLE_UNLOCKED();

	
	for (i = 0; i < MAX_NR_CON_DRIVER; i++) {
		con_driver = &registered_con_driver[i];

		if (con_driver->con == csw) {
			desc = con_driver->desc;
			retval = 0;
			break;
		}
	}

	if (retval)
		goto err;

	if (!(con_driver->flag & CON_DRIVER_FLAG_INIT)) {
		csw->con_startup();
		con_driver->flag |= CON_DRIVER_FLAG_INIT;
	}

	if (deflt) {
		if (conswitchp)
			module_put(conswitchp->owner);

		__module_get(owner);
		conswitchp = csw;
	}

	first = max(first, con_driver->first);
	last = min(last, con_driver->last);

	for (i = first; i <= last; i++) {
		int old_was_color;
		struct vc_data *vc = vc_cons[i].d;

		if (con_driver_map[i])
			module_put(con_driver_map[i]->owner);
		__module_get(owner);
		con_driver_map[i] = csw;

		if (!vc || !vc->vc_sw)
			continue;

		j = i;

		if (con_is_visible(vc)) {
			k = i;
			save_screen(vc);
		}

		old_was_color = vc->vc_can_do_color;
		vc->vc_sw->con_deinit(vc);
		vc->vc_origin = (unsigned long)vc->vc_screenbuf;
		visual_init(vc, i, 0);
		set_origin(vc);
		update_attr(vc);

		
		if (old_was_color != vc->vc_can_do_color)
			clear_buffer_attributes(vc);
	}

	if (j >= 0) {
		struct vc_data *vc = vc_cons[j].d;

		if (k >= 0) {
			vc = vc_cons[k].d;
			update_screen(vc);
		}
	} else {
		pr_cont("to %s\n", desc);
	}

	retval = 0;
err:
	module_put(owner);
	return retval;
};

static inline int vt_bind(struct con_driver *con)
{
	return 0;
}
static inline int vt_unbind(struct con_driver *con)
{
	return 0;
}

static ssize_t store_bind(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct con_driver *con = dev_get_drvdata(dev);
	int bind = simple_strtoul(buf, NULL, 0);

	console_lock();

	if (bind)
		vt_bind(con);
	else
		vt_unbind(con);

	console_unlock();

	return count;
}

static ssize_t show_bind(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct con_driver *con = dev_get_drvdata(dev);
	int bind;

	console_lock();
	bind = con_is_bound(con->con);
	console_unlock();

	return snprintf(buf, PAGE_SIZE, "%i\n", bind);
}

static ssize_t show_name(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct con_driver *con = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%s %s\n",
			(con->flag & CON_DRIVER_FLAG_MODULE) ? "(M)" : "(S)",
			 con->desc);

}

static DEVICE_ATTR(bind, S_IRUGO|S_IWUSR, show_bind, store_bind);
static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static struct attribute *con_dev_attrs[] = {
	&dev_attr_bind.attr,
	&dev_attr_name.attr,
	NULL
};

ATTRIBUTE_GROUPS(con_dev);

static int vtconsole_init_device(struct con_driver *con)
{
	con->flag |= CON_DRIVER_FLAG_ATTR;
	return 0;
}

int con_is_bound(const struct consw *csw)
{
	int i, bound = 0;

	WARN_CONSOLE_UNLOCKED();

	for (i = 0; i < MAX_NR_CONSOLES; i++) {
		if (con_driver_map[i] == csw) {
			bound = 1;
			break;
		}
	}

	return bound;
}

bool con_is_visible(const struct vc_data *vc)
{
	WARN_CONSOLE_UNLOCKED();

	return *vc->vc_display_fg == vc;
}

int con_debug_enter(struct vc_data *vc)
{
	return 0;
}

int con_debug_leave(void)
{
	return 0;
}

static int do_register_con_driver(const struct consw *csw, int first, int last)
{
	/* Stub: console driver registration not needed for minimal kernel */
	return -ENODEV;
}

int do_unregister_con_driver(const struct consw *csw)
{
	/* Stub: console driver unregistration not needed for minimal kernel */
	return -ENODEV;
}

static void con_driver_unregister_callback(struct work_struct *ignored)
{
	/* Stub: no console driver unregistration in minimal kernel */
}

int do_take_over_console(const struct consw *csw, int first, int last, int deflt)
{
	int err;

	err = do_register_con_driver(csw, first, last);
	
	if (err == -EBUSY)
		err = 0;
	if (!err)
		do_bind_con_driver(csw, first, last, deflt);

	return err;
}

void give_up_console(const struct consw *csw)
{
}

static int __init vtconsole_class_init(void)
{
	int i;

	vtconsole_class = class_create(THIS_MODULE, "vtconsole");
	if (IS_ERR(vtconsole_class)) {
		vtconsole_class = NULL;
	}

	
	for (i = 0; i < MAX_NR_CON_DRIVER; i++) {
		struct con_driver *con = &registered_con_driver[i];

		if (con->con && !con->dev) {
			con->dev =
				device_create_with_groups(vtconsole_class, NULL,
							  MKDEV(0, con->node),
							  con, con_dev_groups,
							  "vtcon%i", con->node);

			if (IS_ERR(con->dev)) {
				con->dev = NULL;
			} else {
				vtconsole_init_device(con);
			}
		}
	}

	return 0;
}
postcore_initcall(vtconsole_class_init);

#endif


void do_blank_screen(int entering_gfx)
{
	/* Stubbed: screen blanking not needed for minimal boot */
}

void do_unblank_screen(int leaving_gfx)
{
	/* Stubbed: screen unblanking not needed for minimal boot */
}

void unblank_screen(void)
{
	do_unblank_screen(0);
}

static void blank_screen_t(struct timer_list *unused)
{
	/* Stub: no screen blanking in minimal kernel */
}

void poke_blanked_console(void)
{
	/* Stub: console blanking poke not needed for minimal kernel */
}

static void set_palette(struct vc_data *vc)
{
	/* Stub: no palette setting in minimal kernel */
}

int con_set_cmap(unsigned char __user *arg)
{
	return -EINVAL;
}

int con_get_cmap(unsigned char __user *arg)
{
	return -EINVAL;
}

void reset_palette(struct vc_data *vc)
{
	int j, k;
	for (j=k=0; j<16; j++) {
		vc->vc_palette[k++] = default_red[j];
		vc->vc_palette[k++] = default_grn[j];
		vc->vc_palette[k++] = default_blu[j];
	}
	set_palette(vc);
}

int con_font_op(struct vc_data *vc, struct console_font_op *op)
{
	return -ENOSYS;
}

u16 screen_glyph(const struct vc_data *vc, int offset)
{
	u16 w = scr_readw(screenpos(vc, offset, true));
	u16 c = w & 0xff;

	if (w & vc->vc_hi_font_mask)
		c |= 0x100;
	return c;
}

u32 screen_glyph_unicode(const struct vc_data *vc, int n)
{
	struct uni_screen *uniscr = get_vc_uniscr(vc);

	if (uniscr)
		return uniscr->lines[n / vc->vc_cols][n % vc->vc_cols];
	return inverse_translate(vc, screen_glyph(vc, n * 2), 1);
}

unsigned short *screen_pos(const struct vc_data *vc, int w_offset, bool viewed)
{
	return screenpos(vc, 2 * w_offset, viewed);
}

void getconsxy(const struct vc_data *vc, unsigned char xy[static 2])
{
	
	xy[0] = min(vc->state.x, 0xFFu);
	xy[1] = min(vc->state.y, 0xFFu);
}

void putconsxy(struct vc_data *vc, unsigned char xy[static const 2])
{
	/* Stub: cursor positioning not needed for minimal kernel */
}

u16 vcs_scr_readw(const struct vc_data *vc, const u16 *org)
{
	if ((unsigned long)org == vc->vc_pos && softcursor_original != -1)
		return softcursor_original;
	return scr_readw(org);
}

void vcs_scr_writew(struct vc_data *vc, u16 val, u16 *org)
{
	/* Stub: VCS screen write not needed for minimal kernel */
	scr_writew(val, org);
}

void vcs_scr_updated(struct vc_data *vc)
{
	notify_update(vc);
}

void vc_scrolldelta_helper(struct vc_data *c, int lines,
		unsigned int rolled_over, void *base, unsigned int size)
{
	/* Stubbed: scrollback not needed for minimal boot */
	if (!lines)
		c->vc_visible_origin = c->vc_origin;
}

#ifndef VT_SINGLE_DRIVER
#endif
