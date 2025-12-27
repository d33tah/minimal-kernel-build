 
#ifndef _ASM_X86_IO_H
#define _ASM_X86_IO_H

/* ARCH_HAS_IOREMAP_WC, ARCH_HAS_IOREMAP_WT removed - unused */

#include <linux/string.h>
#include <linux/compiler.h>
#include <linux/cc_platform.h>
#include <asm/page.h>
#include <asm/early_ioremap.h>
#include <asm/pgtable_types.h>
#include <asm/shared/io.h>

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
/* __read*, __write*, relaxed versions, __raw versions removed - unused */
/* ARCH_HAS_VALID_PHYS_ADDR_RANGE, valid_*_range removed - unused */

 

static inline phys_addr_t virt_to_phys(volatile void *address)
{
	return __pa(address);
}
#define virt_to_phys virt_to_phys

 

static inline void *phys_to_virt(phys_addr_t address)
{
	return __va(address);
}
#define phys_to_virt phys_to_virt

#define page_to_phys(page)    ((dma_addr_t)page_to_pfn(page) << PAGE_SHIFT)

extern void __iomem *ioremap_prot(resource_size_t offset, unsigned long size, unsigned long prot_val);
#define ioremap_prot ioremap_prot
/* ioremap_uc, ioremap_cache, ioremap_encrypted declarations removed - no callers */

void __iomem *ioremap(resource_size_t offset, unsigned long size);
#define ioremap ioremap

extern void iounmap(volatile void __iomem *addr);
#define iounmap iounmap

#ifdef __KERNEL__

void memcpy_fromio(void *, const volatile void __iomem *, size_t);
void memcpy_toio(volatile void __iomem *, const void *, size_t);
void memset_io(volatile void __iomem *, int, size_t);

#define memcpy_fromio memcpy_fromio
#define memcpy_toio memcpy_toio
#define memset_io memset_io



 
#define __ISA_IO_base ((char __iomem *)(PAGE_OFFSET))

#endif  

extern void native_io_delay(void);
extern void io_delay_init(void);


static inline void slow_down_io(void)
{
	native_io_delay();
#ifdef REALLY_SLOW_IO
	native_io_delay();
	native_io_delay();
	native_io_delay();
#endif
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

/* xlate_dev_mem_ptr, unxlate_dev_mem_ptr removed - unused */
/* ioremap_change_attr, ioremap_wc, ioremap_wt removed - no callers */

#define IO_SPACE_LIMIT 0xffff

/* ioport_map/unmap, ioremap_np removed - unused */

#endif  
