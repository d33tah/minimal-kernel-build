#ifndef _VT_KERN_H
#define _VT_KERN_H

/* Inlined from linux/vt.h */
#define MIN_NR_CONSOLES 1
#define MAX_NR_CONSOLES	2

#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
/* linux/mm.h removed - no mm types used */
/* consolemap.h inlined - only LAT1_MAP, GRAF_MAP and set_translate used */
#define LAT1_MAP 0
#define GRAF_MAP 1
static inline unsigned short *set_translate(int m, struct vc_data *vc) { return NULL; }
#include <linux/notifier.h>

struct uni_pagedir;
struct uni_screen;

/* NPAR removed - vc_par array removed */
/* VC_TABSTOPS_COUNT removed - vc_tab_stop bitmap removed */

enum vc_intensity {
	VCI_HALF_BRIGHT,
	VCI_NORMAL,
	VCI_BOLD,
	/* VCI_MASK removed - never used */
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
	/* vc_scan_lines removed - write-only, never read */
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
	/* vc_npar, vc_par[NPAR] removed - escape sequence parsing removed */
	/* vt_mode, vt_pid, vt_newvt, paste_wait removed - write-only (never read) */
	unsigned int	vc_disp_ctrl	: 1;
	/* vc_toggle_meta removed - write-only (meta key toggle never used) */
	unsigned int	vc_decscnm	: 1;
	/* vc_decom removed - always 0 (origin mode disabled) */
	unsigned int	vc_decawm	: 1;
	unsigned int	vc_deccm	: 1;
	/* vc_decim removed - write-only (insert mode never used) */
	/* vc_priv removed - write-only, only ever set to EPecma (0) */
	unsigned int	vc_need_wrap	: 1;
	unsigned int	vc_can_do_color	: 1;
	/* vc_report_mouse removed - write-only (never read) */
	unsigned char	vc_utf		: 1;
	/* vc_utf_count removed - write-only, UTF accumulation unused */
	/* vc_utf_char removed - never used */
	/* vc_tab_stop bitmap removed - tab handling unused, write-only */
	unsigned char   vc_palette[16*3];
	unsigned short * vc_translate;
	/* vc_resize_user, vc_bell_pitch, vc_bell_duration, vc_cur_blink_ms removed - unused */
	struct vc_data **vc_display_fg;
	struct uni_pagedir *vc_uni_pagedir;
	struct uni_pagedir **vc_uni_pagedir_loc;
	struct uni_screen *vc_uni_screen;
};

struct vc {
	struct vc_data *d;
	/* SAK_work removed - work was initialized but never scheduled */
};

extern struct vc vc_cons [MAX_NR_CONSOLES];

#define CUR_SIZE(c)		 ((c) & 0x00000f)
# define CUR_NONE			       1
# define CUR_UNDERLINE			       2
/* CUR_LOWER_THIRD, CUR_LOWER_HALF, CUR_TWO_THIRDS removed - never used */
#define CUR_SW				0x000010
#define CUR_ALWAYS_BG			0x000020
#define CUR_INVERT_FG_BG		0x000040
#define CUR_FG				0x000700
#define CUR_BG				0x007000
#define CUR_CHANGE(c)		 ((c) & 0x00ff00)
#define CUR_SET(c)		(((c) & 0xff0000) >> 8)

bool con_is_visible(const struct vc_data *vc);


extern int fg_console;
/* last_console, want_console removed - never read */


int vc_cons_allocated(unsigned int console);
int vc_resize(struct vc_data *vc, unsigned int cols, unsigned int lines);
void reset_palette(struct vc_data *vc);
/* do_unblank_screen, unblank_screen removed - never called */

/* Removed unused consolemap functions: con_set_trans_old, con_get_trans_old,
   con_set_trans_new, con_get_trans_new, con_clear_unimap, con_set_unimap,
   con_get_unimap, con_set_default_unimap, con_free_unimap, con_copy_unimap */


/* vty_init removed - tty_init was removed, so vty_init is dead */

/* default_utf8 removed - only used in vt.c */
extern int global_cursor_default;


/* struct vt_notifier_param removed - never used */

/* hide_boot_cursor, vt_do_diacrit, vt_do_kdskbmode, vt_do_kdskbmeta,
   vt_do_kbkeycode_ioctl, vt_do_kdsk_ioctl, vt_do_kdgkb_ioctl, vt_do_kdskled,
   vt_do_kdgkbmode, vt_do_kdgkbmeta, vt_get_shift_state, vt_get_leds,
   vt_set_led_state removed - never called */

/* vt_reset_unicode, vt_reset_keyboard, vt_kbd_con_start, vt_kbd_con_stop,
   vt_get_kbd_mode_bit, vt_set_kbd_mode_bit, vt_clr_kbd_mode_bit removed */
void vc_scrolldelta_helper(struct vc_data *c, int lines,
		unsigned int rolled_over, void *_base, unsigned int size);

#endif  
