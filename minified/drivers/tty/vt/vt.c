
#include <linux/module.h>
#include <linux/console.h>
#include <linux/vt_kern.h>
static inline void scr_memsetw(u16 *s, u16 c, unsigned int count)
{
	memset16(s, c, count / 2);
}
const struct consw *conswitchp;

struct vc vc_cons[MAX_NR_CONSOLES];

static void vc_init(struct vc_data *vc, unsigned int rows, unsigned int cols,
		    int do_clear);
static void gotoxy(struct vc_data *vc, int new_x, int new_y);
static void reset_terminal(struct vc_data *vc, int do_clear);

int fg_console;

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
	__module_get(vc->vc_sw->owner);
	vc->vc_sw->con_init(vc, init);
	vc->vc_size_row = vc->vc_cols << 1;
	vc->vc_screenbuf_size = vc->vc_rows * vc->vc_size_row;
}

int vc_resize(struct vc_data *vc, unsigned int cols, unsigned int rows)
{
	/* Minimal stub: static console doesn't need resize */
	return 0;
}

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
	vc->vc_need_wrap = 0;
}

static void reset_terminal(struct vc_data *vc, int do_clear)
{
	vc->vc_top = 0;
	vc->vc_bottom = vc->vc_rows;
	vc->vc_state = 0;
	vc->vc_need_wrap = 0;

	vc->state.color = vc->vc_def_color;
	update_attr(vc);

	gotoxy(vc, 0, 0);
	if (do_clear)
		csi_J(vc, 2);
}

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
	vc->vc_def_color = default_color;
	reset_terminal(vc, do_clear);
}

static int __init con_init(void)
{
	const char *display_desc = NULL;
	struct vc_data *vc;
	unsigned int currcons = 0;

	console_lock();

	if (!conswitchp)
		conswitchp = &dummy_con;
	display_desc = conswitchp->con_startup();
	if (!display_desc) {
		fg_console = 0;
		console_unlock();
		return 0;
	}

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
	vc = vc_cons[currcons].d;
	set_origin(vc);
	if (vc->vc_sw->con_save_screen)
		vc->vc_sw->con_save_screen(vc);
	gotoxy(vc, vc->state.x, vc->state.y);
	csi_J(vc, 0);
	set_origin(vc);
	vc->vc_sw->con_switch(vc);

	console_unlock();

	return 0;
}
console_initcall(con_init);
