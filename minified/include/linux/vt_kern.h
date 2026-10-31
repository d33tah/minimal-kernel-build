#ifndef _VT_KERN_H
#define _VT_KERN_H

#include <linux/tty.h>
#include <linux/mm.h>

/* The one virtual console: a text buffer of vc_rows x vc_cols cells. */
struct vc_data {
	const struct consw *vc_sw;
	unsigned int vc_cols;
	unsigned int vc_rows;
	unsigned int vc_size_row;
	unsigned int vc_x, vc_y;
	unsigned long vc_origin;
	unsigned long vc_pos;
	unsigned short vc_erase_char;
	unsigned int vc_need_wrap : 1;
};

int vty_init(void);

#endif
