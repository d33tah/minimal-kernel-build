#ifndef _LINUX_SELECTION_H_
#define _LINUX_SELECTION_H_

#include <linux/string.h>

#define VGA_MAP_MEM(x, s) ((unsigned long)phys_to_virt(x))

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

extern const unsigned char color_table[];

#endif
