
#ifndef _LINUX_SELECTION_H_
#define _LINUX_SELECTION_H_

/* Inlined from vt_buffer.h */
#include <linux/string.h>

#include <asm/vga.h>

#define scr_writew(val, addr) (*(addr) = (val))
#define scr_readw(addr) (*(addr))

static inline void scr_memsetw(u16 *s, u16 c, unsigned int count)
{
	memset16(s, c, count / 2);
}

static inline void scr_memmovew(u16 *d, const u16 *s, unsigned int count)
{
	memmove(d, s, count);
}

#endif
