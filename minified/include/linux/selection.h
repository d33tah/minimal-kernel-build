
#ifndef _LINUX_SELECTION_H_
#define _LINUX_SELECTION_H_

/* Inlined from vt_buffer.h */
#include <linux/string.h>

/* Inlined from asm/vga.h */
#define VGA_MAP_MEM(x, s) ((unsigned long)phys_to_virt(x))

/* VT_BUF_HAVE_RW, VT_BUF_HAVE_MEMSETW, VT_BUF_HAVE_MEMCPYW,
 * VT_BUF_HAVE_MEMMOVEW not defined - use simple implementations */
#define scr_writew(val, addr) (*(addr) = (val))
#define scr_readw(addr) (*(addr))

static inline void scr_memsetw(u16 *s, u16 c, unsigned int count)
{
	memset16(s, c, count / 2);
}

static inline void scr_memcpyw(u16 *d, const u16 *s, unsigned int count)
{
	memcpy(d, s, count);
}

static inline void scr_memmovew(u16 *d, const u16 *s, unsigned int count)
{
	memmove(d, s, count);
}

/* clear_selection, vc_is_sel, set_selection_*, paste_selection, sel_loadlut,
 * mouse_reporting, mouse_report removed - never called */
/* console_blanked removed - never assigned, always 0 */

extern const unsigned char color_table[];
/* default_red/grn/blu declarations removed - arrays removed (all zeros, replaced with memset) */

/* complement_pos, invert_screen, screen_pos, screen_glyph, screen_glyph_unicode,
 * getconsxy, putconsxy, vcs_scr_readw, vcs_scr_writew, vcs_scr_updated removed - never called */

#endif
