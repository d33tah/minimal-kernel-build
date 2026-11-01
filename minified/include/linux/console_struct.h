/* SPDX-License-Identifier: GPL-2.0 */
/*
 * console_struct.h
 *
 * Data structure describing single virtual console except for data
 * used by vt.c.
 *
 * Fields marked with [#] must be set by the low-level driver.
 * Fields marked with [!] can be changed by the low-level driver
 * to achieve effects such as fast scrolling by changing the origin.
 */

#ifndef _LINUX_CONSOLE_STRUCT_H
#define _LINUX_CONSOLE_STRUCT_H

/**
 * struct vc_state -- state of a VC
 * @x: cursor's x-position
 * @y: cursor's y-position
 */
struct vc_state {
	unsigned int x, y;
};

/*
 * Example: vc_data of a console that was scrolled 3 lines down.
 *
 *                              Console buffer
 * vc_screenbuf ---------> +----------------------+-.
 *                         | initializing W       |  \
 *                         | initializing X       |   |
 *                         | initializing Y       |    > scroll-back area
 *                         | initializing Z       |   |
 *                         |                      |  /
 * vc_visible_origin ---> ^+----------------------+-:
 * (changes by scroll)    || Welcome to linux     |  \
 *                        ||                      |   |
 *           vc_rows --->< | login: root          |   |  visible on console
 *                        || password:            |    > (vc_screenbuf_size is
 * vc_origin -----------> ||                      |   |   vc_size_row * vc_rows)
 * (start when no scroll) || Last login: 12:28    |  /
 *                        v+----------------------+-:
 *                         | Have a lot of fun... |  \
 * vc_pos -----------------|--------v             |   > scroll-front area
 *                         | ~ # cat_             |  /
 * vc_scr_end -----------> +----------------------+-:
 * (vc_origin +            |                      |  \ EMPTY, to be filled by
 *  vc_screenbuf_size)     |                      |  / vc_video_erase_char
 *                         +----------------------+-'
 *                         <---- 2 * vc_cols ----->
 *                         <---- vc_size_row ----->
 *
 * Note that every character in the console buffer is accompanied with an
 * attribute in the buffer right after the character. This is not depicted
 * in the figure.
 */
struct vc_data {
	struct vc_state state;

	unsigned int vc_cols; /* [#] Console size */
	unsigned int vc_rows;
	unsigned int vc_size_row; /* Bytes per row */
	unsigned long vc_origin; /* [!] Start of real screen */
	const struct consw *vc_sw;
	unsigned long vc_pos; /* Cursor address */
	unsigned short vc_video_erase_char; /* Background erase character */
	unsigned int vc_need_wrap : 1;
};

#endif /* _LINUX_CONSOLE_STRUCT_H */
