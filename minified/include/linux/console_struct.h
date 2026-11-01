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

#endif
