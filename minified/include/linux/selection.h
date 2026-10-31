
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
	memset16(s, c, count / 2);
}
#endif

#ifndef VT_BUF_HAVE_MEMMOVEW
static inline void scr_memmovew(u16 *d, const u16 *s, unsigned int count)
{
	memmove(d, s, count);
}
#endif

#endif
