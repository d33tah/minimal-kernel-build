 
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

build_mmio_read(__readb, "b", unsigned char, "=q", )
build_mmio_read(__readw, "w", unsigned short, "=r", )
build_mmio_read(__readl, "l", unsigned int, "=r", )

build_mmio_write(writeb, "b", unsigned char, "q", :"memory")
build_mmio_write(writew, "w", unsigned short, "r", :"memory")
build_mmio_write(writel, "l", unsigned int, "r", :"memory")

build_mmio_write(__writeb, "b", unsigned char, "q", )
build_mmio_write(__writew, "w", unsigned short, "r", )
build_mmio_write(__writel, "l", unsigned int, "r", )

#define readb readb
#define readw readw
#define readl readl
#define readb_relaxed(a) __readb(a)
#define readw_relaxed(a) __readw(a)
#define readl_relaxed(a) __readl(a)
#define writeb writeb
#define writew writew
#define writel writel
#define writeb_relaxed(v, a) __writeb(v, a)
#define writew_relaxed(v, a) __writew(v, a)
#define writel_relaxed(v, a) __writel(v, a)
/* __raw_read*, __raw_write* removed - unused */
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


#define BUILDIO(bwl, bw, type)						\
static inline void out##bwl##_p(type value, u16 port)			\
{									\
	out##bwl(value, port);						\
	slow_down_io();							\
}									\
									\
static inline type in##bwl##_p(u16 port)				\
{									\
	type value = in##bwl(port);					\
	slow_down_io();							\
	return value;							\
}									\
									\
static inline void outs##bwl(u16 port, const void *addr, unsigned long count) \
{									\
	if (cc_platform_has(CC_ATTR_GUEST_UNROLL_STRING_IO)) {		\
		type *value = (type *)addr;				\
		while (count) {						\
			out##bwl(*value, port);				\
			value++;					\
			count--;					\
		}							\
	} else {							\
		asm volatile("rep; outs" #bwl				\
			     : "+S"(addr), "+c"(count)			\
			     : "d"(port) : "memory");			\
	}								\
}									\
									\
static inline void ins##bwl(u16 port, void *addr, unsigned long count)	\
{									\
	if (cc_platform_has(CC_ATTR_GUEST_UNROLL_STRING_IO)) {		\
		type *value = (type *)addr;				\
		while (count) {						\
			*value = in##bwl(port);				\
			value++;					\
			count--;					\
		}							\
	} else {							\
		asm volatile("rep; ins" #bwl				\
			     : "+D"(addr), "+c"(count)			\
			     : "d"(port) : "memory");			\
	}								\
}

BUILDIO(b, b, u8)
BUILDIO(w, w, u16)
BUILDIO(l,  , u32)
#undef BUILDIO

#define inb_p inb_p
#define inw_p inw_p
#define inl_p inl_p
#define insb insb
#define insw insw
#define insl insl

#define outb_p outb_p
#define outw_p outw_p
#define outl_p outl_p
#define outsb outsb
#define outsw outsw
#define outsl outsl

extern void *xlate_dev_mem_ptr(phys_addr_t phys);
extern void unxlate_dev_mem_ptr(phys_addr_t phys, void *addr);

#define xlate_dev_mem_ptr xlate_dev_mem_ptr
#define unxlate_dev_mem_ptr unxlate_dev_mem_ptr
/* ioremap_change_attr, ioremap_wc, ioremap_wt removed - no callers */

#define IO_SPACE_LIMIT 0xffff

/* ioport_map/unmap removed - declared but never defined/used */

#ifndef ioremap_np
#define ioremap_np ioremap_np
static inline void __iomem *ioremap_np(phys_addr_t offset, size_t size)
{
	return NULL;
}
#endif

/* phys_mem_access_encrypted removed - unused */

#endif  
