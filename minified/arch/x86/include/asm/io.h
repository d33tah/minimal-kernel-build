#ifndef _ASM_X86_IO_H
#define _ASM_X86_IO_H

#include <asm/shared/io.h>

static inline void *phys_to_virt(phys_addr_t address)
{
	return __va(address);
}

extern void native_io_delay(void);

static inline void slow_down_io(void)
{
	native_io_delay();
}

#define BUILDIO(bwl, bw, type)                                                 \
	static inline void out##bwl##_p(type value, u16 port)                  \
	{                                                                      \
		out##bwl(value, port);                                         \
		slow_down_io();                                                \
	}                                                                      \
	static inline type in##bwl##_p(u16 port)                               \
	{                                                                      \
		type value = in##bwl(port);                                    \
		slow_down_io();                                                \
		return value;                                                  \
	}                                                                      \
	static inline void outs##bwl(u16 port, const void *addr,               \
				     unsigned long count)                      \
	{                                                                      \
		asm volatile("rep; outs" #bwl                                  \
			     : "+S"(addr), "+c"(count)                         \
			     : "d"(port)                                       \
			     : "memory");                                      \
	}                                                                      \
	static inline void ins##bwl(u16 port, void *addr, unsigned long count) \
	{                                                                      \
		asm volatile("rep; ins" #bwl                                   \
			     : "+D"(addr), "+c"(count)                         \
			     : "d"(port)                                       \
			     : "memory");                                      \
	}

BUILDIO(b, b, u8)
#undef BUILDIO

#endif
