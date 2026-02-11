#ifndef _VT_KERN_H
#define _VT_KERN_H

#define MIN_NR_CONSOLES 1
#define MAX_NR_CONSOLES	2

/* inlined from linux/kd.h */
#ifndef KD_TEXT
#define KD_TEXT		0x00
#define KD_GRAPHICS	0x01
#endif
#include <linux/tty.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/notifier.h>

struct uni_pagedir;

struct vc_state {
	unsigned int	x, y;
	unsigned char	color;
};

struct vc_data {
	struct tty_port port;

	struct vc_state state;

	unsigned short	vc_num;
	unsigned int	vc_cols;
	unsigned int	vc_rows;
	unsigned int	vc_size_row;
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
	unsigned short	vc_complement_mask;
	unsigned short	vc_s_complement_mask;
	unsigned long	vc_pos;
	unsigned short	vc_video_erase_char;
	unsigned int	vc_state;
	unsigned int	vc_need_wrap	: 1;
	unsigned int	vc_can_do_color	: 1;
	struct vc_data **vc_display_fg;
	struct uni_pagedir *vc_uni_pagedir;
	struct uni_pagedir **vc_uni_pagedir_loc;
};

struct vc {
	struct vc_data *d;
};

bool con_is_visible(const struct vc_data *vc);
int vc_resize(struct vc_data *vc, unsigned int cols, unsigned int lines);
#endif
