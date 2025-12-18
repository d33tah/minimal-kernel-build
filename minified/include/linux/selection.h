
#ifndef _LINUX_SELECTION_H_
#define _LINUX_SELECTION_H_

/* Inlined from vt_buffer.h */
#include <linux/string.h>

#include <asm/vga.h>

#ifndef VT_BUF_HAVE_RW
#define scr_writew(val, addr) (*(addr) = (val))
#define scr_readw(addr) (*(addr))
#endif

#ifndef VT_BUF_HAVE_MEMSETW
static inline void scr_memsetw(u16 *s, u16 c, unsigned int count)
{
#ifdef VT_BUF_HAVE_RW
	count /= 2;
	while (count--)
		scr_writew(c, s++);
#else
	memset16(s, c, count / 2);
#endif
}
#endif

#ifndef VT_BUF_HAVE_MEMCPYW
static inline void scr_memcpyw(u16 *d, const u16 *s, unsigned int count)
{
#ifdef VT_BUF_HAVE_RW
	count /= 2;
	while (count--)
		scr_writew(scr_readw(s++), d++);
#else
	memcpy(d, s, count);
#endif
}
#endif

#ifndef VT_BUF_HAVE_MEMMOVEW
static inline void scr_memmovew(u16 *d, const u16 *s, unsigned int count)
{
#ifdef VT_BUF_HAVE_RW
	if (d < s)
		scr_memcpyw(d, s, count);
	else {
		count /= 2;
		d += count;
		s += count;
		while (count--)
			scr_writew(scr_readw(--s), --d);
	}
#else
	memmove(d, s, count);
#endif
}
#endif
/* End of inlined vt_buffer.h content */

struct tty_struct;
struct vc_data;

static inline void clear_selection(void) { }
/* set_selection_user, set_selection_kernel, paste_selection, sel_loadlut,
 * mouse_reporting, mouse_report removed - never called */

static inline bool vc_is_sel(struct vc_data *vc) { return false; }

extern int console_blanked;

extern const unsigned char color_table[];
extern unsigned char default_red[];
extern unsigned char default_grn[];
extern unsigned char default_blu[];

/* complement_pos, invert_screen, screen_pos, screen_glyph, screen_glyph_unicode,
 * getconsxy, putconsxy, vcs_scr_readw, vcs_scr_writew, vcs_scr_updated removed - never called */

#endif
