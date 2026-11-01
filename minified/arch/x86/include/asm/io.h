/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IO_H
#define _ASM_X86_IO_H

#include <asm/shared/io.h>

/**
 *	phys_to_virt	-	map physical address to virtual
 *	@address: address to remap
 *
 *	The returned virtual address is a current CPU mapping for
 *	the memory address given. It is only valid to use this function on
 *	addresses that have a kernel mapping
 *
 *	This function does not handle bus mappings for DMA transfers. In
 *	almost all conceivable cases a device driver should not be using
 *	this function
 */

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
