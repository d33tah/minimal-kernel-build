/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_SCREEN_INFO_H
#define _UAPI_SCREEN_INFO_H

/*
 * These are set up by the setup-routine at boot-time:
 */

struct screen_info {
	__u8 orig_x; /* 0x00 */
	__u8 orig_y; /* 0x01 */
	__u16 ext_mem_k; /* 0x02 */
	__u16 orig_video_page; /* 0x04 */
	__u8 orig_video_mode; /* 0x06 */
	__u8 orig_video_cols; /* 0x07 */
	__u8 flags; /* 0x08 */
	__u8 unused2; /* 0x09 */
	__u16 orig_video_ega_bx; /* 0x0a */
	__u16 unused3; /* 0x0c */
	__u8 orig_video_lines; /* 0x0e */
	__u8 orig_video_isVGA; /* 0x0f */
	__u16 orig_video_points; /* 0x10 */

	/* VESA graphic mode -- linear frame buffer */
	__u16 lfb_width; /* 0x12 */
	__u16 lfb_height; /* 0x14 */
	__u16 lfb_depth; /* 0x16 */
	__u32 lfb_base; /* 0x18 */
	__u32 lfb_size; /* 0x1c */
	__u16 cl_magic, cl_offset; /* 0x20 */
	__u16 lfb_linelength; /* 0x24 */
	__u8 red_size; /* 0x26 */
	__u8 red_pos; /* 0x27 */
	__u8 green_size; /* 0x28 */
	__u8 green_pos; /* 0x29 */
	__u8 blue_size; /* 0x2a */
	__u8 blue_pos; /* 0x2b */
	__u8 rsvd_size; /* 0x2c */
	__u8 rsvd_pos; /* 0x2d */
	__u16 vesapm_seg; /* 0x2e */
	__u16 vesapm_off; /* 0x30 */
	__u16 pages; /* 0x32 */
	__u16 vesa_attributes; /* 0x34 */
	__u32 capabilities; /* 0x36 */
	__u32 ext_lfb_base; /* 0x3a */
	__u8 _reserved[2]; /* 0x3e */
} __attribute__((packed));

#define VIDEO_TYPE_VLFB 0x23 /* VESA VGA in graphic mode	*/
#define VIDEO_TYPE_EFI 0x70 /* EFI graphic mode		*/

#endif /* _UAPI_SCREEN_INFO_H */
