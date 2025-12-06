
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

/* From uapi/linux/tiocl.h - inlined */
#define TIOCL_SETSEL	2
#define 	TIOCL_SELCHAR	0
#define 	TIOCL_SELWORD	1
#define 	TIOCL_SELLINE	2
#define 	TIOCL_SELPOINTER	3
#define 	TIOCL_SELCLEAR	4
#define 	TIOCL_SELMOUSEREPORT	16
#define 	TIOCL_SELBUTTONMASK	15
struct tiocl_selection {
	unsigned short xs;
	unsigned short ys;
	unsigned short xe;
	unsigned short ye;
	unsigned short sel_mode;
};
#define TIOCL_PASTESEL	3
#define TIOCL_UNBLANKSCREEN	4
#define TIOCL_SELLOADLUT	5
#define TIOCL_GETSHIFTSTATE	6
#define TIOCL_GETMOUSEREPORTING	7
#define TIOCL_SETVESABLANK	10
#define TIOCL_SETKMSGREDIRECT	11
#define TIOCL_GETFGCONSOLE	12
#define TIOCL_SCROLLCONSOLE	13
#define TIOCL_BLANKSCREEN	14
#define TIOCL_BLANKEDSCREEN	15
#define TIOCL_GETKMSGREDIRECT	17

struct tty_struct;
struct vc_data;

static inline void clear_selection(void) { }
extern int set_selection_user(const struct tiocl_selection __user *sel,
			      struct tty_struct *tty);
extern int set_selection_kernel(struct tiocl_selection *v,
				struct tty_struct *tty);
extern int paste_selection(struct tty_struct *tty);
extern int sel_loadlut(char __user *p);
extern int mouse_reporting(void);
extern void mouse_report(struct tty_struct * tty, int butt, int mrx, int mry);

static inline bool vc_is_sel(struct vc_data *vc) { return false; }

extern int console_blanked;

extern const unsigned char color_table[];
extern unsigned char default_red[];
extern unsigned char default_grn[];
extern unsigned char default_blu[];

extern unsigned short *screen_pos(const struct vc_data *vc, int w_offset,
		bool viewed);
extern u16 screen_glyph(const struct vc_data *vc, int offset);
extern u32 screen_glyph_unicode(const struct vc_data *vc, int offset);
extern void complement_pos(struct vc_data *vc, int offset);
extern void invert_screen(struct vc_data *vc, int offset, int count, bool viewed);

extern void getconsxy(const struct vc_data *vc, unsigned char xy[static 2]);
extern void putconsxy(struct vc_data *vc, unsigned char xy[static const 2]);

extern u16 vcs_scr_readw(const struct vc_data *vc, const u16 *org);
extern void vcs_scr_writew(struct vc_data *vc, u16 val, u16 *org);
extern void vcs_scr_updated(struct vc_data *vc);

extern int vc_uniscr_check(struct vc_data *vc);
extern void vc_uniscr_copy_line(const struct vc_data *vc, void *dest,
				bool viewed,
				unsigned int row, unsigned int col,
				unsigned int nr);

#endif
