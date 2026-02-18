 
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

#define build_mmio_read(name, size, type, reg, barrier) \
static inline type name(const volatile void __iomem *addr) \
{ type ret; asm volatile("mov" size " %1,%0":reg (ret) \
:"m" (*(volatile type __force *)addr) barrier); return ret; }

#define build_mmio_write(name, size, type, reg, barrier) \
static inline void name(type val, volatile void __iomem *addr) \
{ asm volatile("mov" size " %0,%1": :reg (val), \
"m" (*(volatile type __force *)addr) barrier); }

build_mmio_read(readb, "b", unsigned char, "=q", :"memory")
build_mmio_read(readw, "w", unsigned short, "=r", :"memory")
build_mmio_read(readl, "l", unsigned int, "=r", :"memory")

build_mmio_write(writeb, "b", unsigned char, "q", :"memory")
build_mmio_write(writew, "w", unsigned short, "r", :"memory")
build_mmio_write(writel, "l", unsigned int, "r", :"memory")

#define readb readb
#define readw readw
#define readl readl
#define writeb writeb
#define writew writew
#define writel writel

static inline void *phys_to_virt(phys_addr_t address)
{
	return __va(address);
}
#define phys_to_virt phys_to_virt

void __iomem *ioremap(resource_size_t offset, unsigned long size);
#define ioremap ioremap

extern void iounmap(volatile void __iomem *addr);
#define iounmap iounmap

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
