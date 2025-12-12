#ifndef _VT_KERN_H
#define _VT_KERN_H


#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/mm.h>
#include <linux/consolemap.h>
#include <linux/notifier.h>

/* --- 2025-12-06 16:55 --- console_struct.h inlined */
struct uni_pagedir;
struct uni_screen;

#define NPAR 16
#define VC_TABSTOPS_COUNT	256U

enum vc_intensity {
	VCI_HALF_BRIGHT,
	VCI_NORMAL,
	VCI_BOLD,
	VCI_MASK = 0x3,
};

struct vc_state {
	unsigned int	x, y;
	unsigned char	color;
	unsigned char	Gx_charset[2];
	unsigned int	charset		: 1;
	enum vc_intensity intensity;
	bool		italic;
	bool		underline;
	bool		blink;
	bool		reverse;
};

struct vc_data {
	struct tty_port port;

	struct vc_state state, saved_state;

	unsigned short	vc_num;
	unsigned int	vc_cols;
	unsigned int	vc_rows;
	unsigned int	vc_size_row;
	unsigned int	vc_scan_lines;
	unsigned int	vc_cell_height;
	unsigned long	vc_origin;
	unsigned long	vc_scr_end;
	unsigned long	vc_visible_origin;
	unsigned int	vc_top, vc_bottom;
	const struct consw *vc_sw;
	unsigned short	*vc_screenbuf;
	unsigned int	vc_screenbuf_size;
	unsigned char	vc_mode;
	unsigned char	vc_attr;
	unsigned char	vc_def_color;
	unsigned char	vc_ulcolor;
	unsigned char   vc_itcolor;
	unsigned char	vc_halfcolor;
	unsigned int	vc_cursor_type;
	unsigned short	vc_complement_mask;
	unsigned short	vc_s_complement_mask;
	unsigned long	vc_pos;
	unsigned short	vc_hi_font_mask;
	struct console_font vc_font;
	unsigned short	vc_video_erase_char;
	unsigned int	vc_state;
	unsigned int	vc_npar,vc_par[NPAR];
	struct vt_mode	vt_mode;
	struct pid 	*vt_pid;
	int		vt_newvt;
	wait_queue_head_t paste_wait;
	unsigned int	vc_disp_ctrl	: 1;
	unsigned int	vc_toggle_meta	: 1;
	unsigned int	vc_decscnm	: 1;
	unsigned int	vc_decom	: 1;
	unsigned int	vc_decawm	: 1;
	unsigned int	vc_deccm	: 1;
	unsigned int	vc_decim	: 1;
	unsigned int	vc_priv		: 3;
	unsigned int	vc_need_wrap	: 1;
	unsigned int	vc_can_do_color	: 1;
	unsigned int	vc_report_mouse : 2;
	unsigned char	vc_utf		: 1;
	unsigned char	vc_utf_count;
		 int	vc_utf_char;
	DECLARE_BITMAP(vc_tab_stop, VC_TABSTOPS_COUNT);
	unsigned char   vc_palette[16*3];
	unsigned short * vc_translate;
	unsigned int    vc_resize_user;
	unsigned int	vc_bell_pitch;
	unsigned int	vc_bell_duration;
	unsigned short	vc_cur_blink_ms;
	struct vc_data **vc_display_fg;
	struct uni_pagedir *vc_uni_pagedir;
	struct uni_pagedir **vc_uni_pagedir_loc;
	struct uni_screen *vc_uni_screen;
};

struct vc {
	struct vc_data *d;
	struct work_struct SAK_work;
};

extern struct vc vc_cons [MAX_NR_CONSOLES];
extern void vc_SAK(struct work_struct *work);

#define CUR_MAKE(size, change, set)	((size) | ((change) << 8) |	\
		((set) << 16))
#define CUR_SIZE(c)		 ((c) & 0x00000f)
# define CUR_DEF			       0
# define CUR_NONE			       1
# define CUR_UNDERLINE			       2
# define CUR_LOWER_THIRD		       3
# define CUR_LOWER_HALF			       4
# define CUR_TWO_THIRDS			       5
# define CUR_BLOCK			       6
#define CUR_SW				0x000010
#define CUR_ALWAYS_BG			0x000020
#define CUR_INVERT_FG_BG		0x000040
#define CUR_FG				0x000700
#define CUR_BG				0x007000
#define CUR_CHANGE(c)		 ((c) & 0x00ff00)
#define CUR_SET(c)		(((c) & 0xff0000) >> 8)

bool con_is_visible(const struct vc_data *vc);
/* --- end console_struct.h inlined --- */

/* kd_mksound, kbd_rate removed - unused */

extern int fg_console, last_console, want_console;


int vc_allocate(unsigned int console);
int vc_cons_allocated(unsigned int console);
int vc_resize(struct vc_data *vc, unsigned int cols, unsigned int lines);
struct vc_data *vc_deallocate(unsigned int console);
void reset_palette(struct vc_data *vc);
void do_unblank_screen(int leaving_gfx);
void unblank_screen(void);
void poke_blanked_console(void);
/* do_blank_screen, con_font_op, con_set_cmap, con_get_cmap removed - never called */
/* scrollback, scrollfront removed - never called */
/* clear_buffer_attributes, update_region, redraw_screen - now static in vt.c */
/* tioclinux removed - never called */

struct unipair;

int con_set_trans_old(unsigned char __user * table);
int con_get_trans_old(unsigned char __user * table);
int con_set_trans_new(unsigned short __user * table);
int con_get_trans_new(unsigned short __user * table);
int con_clear_unimap(struct vc_data *vc);
int con_set_unimap(struct vc_data *vc, ushort ct, struct unipair __user *list);
int con_get_unimap(struct vc_data *vc, ushort ct, ushort __user *uct, struct unipair __user *list);
int con_set_default_unimap(struct vc_data *vc);
void con_free_unimap(struct vc_data *vc);
int con_copy_unimap(struct vc_data *dst_vc, struct vc_data *src_vc);


/* vt_event_post, vt_waitactive removed - unused */
void change_console(struct vc_data *new_vc);
void reset_vc(struct vc_data *vc);
int do_unbind_con_driver(const struct consw *csw, int first, int last,
			 int deflt);
int vty_init(const struct file_operations *console_fops);

/* vt_dont_switch removed - unused */
extern int default_utf8;
extern int global_cursor_default;

/* vt_spawn_console struct and vt_spawn_con removed - never used */
/* vt_move_to_console removed - never called */

struct vt_notifier_param {
	struct vc_data *vc;	 
	unsigned int c;
};

/* hide_boot_cursor, vt_do_diacrit, vt_do_kdskbmode, vt_do_kdskbmeta,
   vt_do_kbkeycode_ioctl, vt_do_kdsk_ioctl, vt_do_kdgkb_ioctl, vt_do_kdskled,
   vt_do_kdgkbmode, vt_do_kdgkbmeta, vt_get_shift_state, vt_get_leds,
   vt_set_led_state removed - never called */

int vt_reset_unicode(unsigned int console);
void vt_reset_keyboard(unsigned int console);
int vt_get_kbd_mode_bit(unsigned int console, int bit);
void vt_set_kbd_mode_bit(unsigned int console, int bit);
void vt_clr_kbd_mode_bit(unsigned int console, int bit);
void vt_kbd_con_start(unsigned int console);
void vt_kbd_con_stop(unsigned int console);

void vc_scrolldelta_helper(struct vc_data *c, int lines,
		unsigned int rolled_over, void *_base, unsigned int size);

#endif  
