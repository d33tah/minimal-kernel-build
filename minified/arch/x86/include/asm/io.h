 
#ifndef _ASM_X86_IO_H
#define _ASM_X86_IO_H

#include <linux/string.h>
#include <linux/compiler.h>
#include <asm/page.h>
extern void early_ioremap_init(void);
extern void early_ioremap_setup(void);
extern void early_ioremap_reset(void);
#include <asm/pgtable_types.h>
#define BUILDIO(bwl, bw, type)						\
static inline void __out##bwl(type value, u16 port)			\
{									\
	asm volatile("out" #bwl " %" #bw "0, %w1"			\
		     : : "a"(value), "Nd"(port));			\
}									\
static inline type __in##bwl(u16 port)					\
{									\
	type value;							\
	asm volatile("in" #bwl " %w1, %" #bw "0"			\
		     : "=a"(value) : "Nd"(port));			\
	return value;							\
}
BUILDIO(b, b, u8)
BUILDIO(w, w, u16)
BUILDIO(l,  , u32)
#undef BUILDIO
#define inb __inb
#define outb __outb
#define outw __outw


static inline void *phys_to_virt(phys_addr_t address)
{
	return __va(address);
}
#define phys_to_virt phys_to_virt


static inline void native_io_delay(void)
{
	asm volatile("outb %al, $0x80");
}

static inline void slow_down_io(void)
{
	native_io_delay();
}

static inline void outb_p(u8 value, u16 port)
{
	outb(value, port);
	slow_down_io();
}

static inline u8 inb_p(u16 port)
{
	u8 value = inb(port);
	slow_down_io();
	return value;
}

#define inb_p inb_p
#define outb_p outb_p

#define IO_SPACE_LIMIT 0xffff

#endif  
