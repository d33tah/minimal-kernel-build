#ifndef __ASM_GENERIC_IO_H
#define __ASM_GENERIC_IO_H

#include <asm/page.h>  
#include <linux/string.h>  
#include <linux/types.h>



#include <asm/mmiowb.h>


#ifndef __io_br
#define __io_br()      barrier()
#endif

#ifndef __io_ar
#ifdef rmb
#define __io_ar(v)      rmb()
#else
#define __io_ar(v)      barrier()
#endif
#endif

#ifndef __io_bw
#ifdef wmb
#define __io_bw()      wmb()
#else
#define __io_bw()      barrier()
#endif
#endif

#ifndef __io_aw
#define __io_aw()      mmiowb_set_pending()
#endif

#ifndef __io_pbw
#define __io_pbw()     __io_bw()
#endif

#ifndef __io_paw
#define __io_paw()     __io_aw()
#endif

#ifndef __io_pbr
#define __io_pbr()     __io_br()
#endif

#ifndef __io_par
#define __io_par(v)     __io_ar(v)
#endif



#ifndef __raw_readb
#define __raw_readb __raw_readb
static inline u8 __raw_readb(const volatile void __iomem *addr)
{
	return *(const volatile u8 __force *)addr;
}
#endif

#ifndef __raw_readw
#define __raw_readw __raw_readw
static inline u16 __raw_readw(const volatile void __iomem *addr)
{
	return *(const volatile u16 __force *)addr;
}
#endif

#ifndef __raw_readl
#define __raw_readl __raw_readl
static inline u32 __raw_readl(const volatile void __iomem *addr)
{
	return *(const volatile u32 __force *)addr;
}
#endif


#ifndef __raw_writeb
#define __raw_writeb __raw_writeb
static inline void __raw_writeb(u8 value, volatile void __iomem *addr)
{
	*(volatile u8 __force *)addr = value;
}
#endif

#ifndef __raw_writew
#define __raw_writew __raw_writew
static inline void __raw_writew(u16 value, volatile void __iomem *addr)
{
	*(volatile u16 __force *)addr = value;
}
#endif

#ifndef __raw_writel
#define __raw_writel __raw_writel
static inline void __raw_writel(u32 value, volatile void __iomem *addr)
{
	*(volatile u32 __force *)addr = value;
}
#endif



#ifndef readb
#define readb readb
static inline u8 readb(const volatile void __iomem *addr)
{
	u8 val;

	__io_br();
	val = __raw_readb(addr);
	__io_ar(val);
	return val;
}
#endif

#ifndef readw
#define readw readw
static inline u16 readw(const volatile void __iomem *addr)
{
	u16 val;

	__io_br();
	val = __le16_to_cpu((__le16 __force)__raw_readw(addr));
	__io_ar(val);
	return val;
}
#endif

#ifndef readl
#define readl readl
static inline u32 readl(const volatile void __iomem *addr)
{
	u32 val;

	__io_br();
	val = __le32_to_cpu((__le32 __force)__raw_readl(addr));
	__io_ar(val);
	return val;
}
#endif


#ifndef writeb
#define writeb writeb
static inline void writeb(u8 value, volatile void __iomem *addr)
{
	__io_bw();
	__raw_writeb(value, addr);
	__io_aw();
}
#endif

#ifndef writew
#define writew writew
static inline void writew(u16 value, volatile void __iomem *addr)
{
	__io_bw();
	__raw_writew((u16 __force)cpu_to_le16(value), addr);
	__io_aw();
}
#endif

#ifndef writel
#define writel writel
static inline void writel(u32 value, volatile void __iomem *addr)
{
	__io_bw();
	__raw_writel((u32 __force)__cpu_to_le32(value), addr);
	__io_aw();
}
#endif


#ifndef readb_relaxed
#define readb_relaxed readb_relaxed
static inline u8 readb_relaxed(const volatile void __iomem *addr)
{
	return __raw_readb(addr);
}
#endif

#ifndef readw_relaxed
#define readw_relaxed readw_relaxed
static inline u16 readw_relaxed(const volatile void __iomem *addr)
{
	return __le16_to_cpu(__raw_readw(addr));
}
#endif

#ifndef readl_relaxed
#define readl_relaxed readl_relaxed
static inline u32 readl_relaxed(const volatile void __iomem *addr)
{
	return __le32_to_cpu(__raw_readl(addr));
}
#endif

#if defined(readq) && !defined(readq_relaxed)
#define readq_relaxed readq_relaxed
static inline u64 readq_relaxed(const volatile void __iomem *addr)
{
	return __le64_to_cpu(__raw_readq(addr));
}
#endif

#ifndef writeb_relaxed
#define writeb_relaxed writeb_relaxed
static inline void writeb_relaxed(u8 value, volatile void __iomem *addr)
{
	__raw_writeb(value, addr);
}
#endif

#ifndef writew_relaxed
#define writew_relaxed writew_relaxed
static inline void writew_relaxed(u16 value, volatile void __iomem *addr)
{
	__raw_writew(cpu_to_le16(value), addr);
}
#endif

#ifndef writel_relaxed
#define writel_relaxed writel_relaxed
static inline void writel_relaxed(u32 value, volatile void __iomem *addr)
{
	__raw_writel(__cpu_to_le32(value), addr);
}
#endif

#if defined(writeq) && !defined(writeq_relaxed)
#define writeq_relaxed writeq_relaxed
static inline void writeq_relaxed(u64 value, volatile void __iomem *addr)
{
	__raw_writeq(__cpu_to_le64(value), addr);
}
#endif

#ifndef PCI_IOBASE
#define PCI_IOBASE ((void __iomem *)0)
#endif

#ifndef IO_SPACE_LIMIT
#define IO_SPACE_LIMIT 0xffff
#endif


#if !defined(inb) && !defined(_inb)
#define _inb _inb
static inline u8 _inb(unsigned long addr)
{
	u8 val;

	__io_pbr();
	val = __raw_readb(PCI_IOBASE + addr);
	__io_par(val);
	return val;
}
#endif

#if !defined(inw) && !defined(_inw)
#define _inw _inw
static inline u16 _inw(unsigned long addr)
{
	u16 val;

	__io_pbr();
	val = __le16_to_cpu((__le16 __force)__raw_readw(PCI_IOBASE + addr));
	__io_par(val);
	return val;
}
#endif

#if !defined(inl) && !defined(_inl)
#define _inl _inl
static inline u32 _inl(unsigned long addr)
{
	u32 val;

	__io_pbr();
	val = __le32_to_cpu((__le32 __force)__raw_readl(PCI_IOBASE + addr));
	__io_par(val);
	return val;
}
#endif

#if !defined(outb) && !defined(_outb)
#define _outb _outb
static inline void _outb(u8 value, unsigned long addr)
{
	__io_pbw();
	__raw_writeb(value, PCI_IOBASE + addr);
	__io_paw();
}
#endif

#if !defined(outw) && !defined(_outw)
#define _outw _outw
static inline void _outw(u16 value, unsigned long addr)
{
	__io_pbw();
	__raw_writew((u16 __force)cpu_to_le16(value), PCI_IOBASE + addr);
	__io_paw();
}
#endif

#if !defined(outl) && !defined(_outl)
#define _outl _outl
static inline void _outl(u32 value, unsigned long addr)
{
	__io_pbw();
	__raw_writel((u32 __force)cpu_to_le32(value), PCI_IOBASE + addr);
	__io_paw();
}
#endif

#include <linux/logic_pio.h>

#ifndef inb
#define inb _inb
#endif

#ifndef inw
#define inw _inw
#endif

#ifndef inl
#define inl _inl
#endif

#ifndef outb
#define outb _outb
#endif

#ifndef outw
#define outw _outw
#endif

#ifndef outl
#define outl _outl
#endif

#ifndef inb_p
#define inb_p inb_p
static inline u8 inb_p(unsigned long addr)
{
	return inb(addr);
}
#endif

#ifndef inw_p
#define inw_p inw_p
static inline u16 inw_p(unsigned long addr)
{
	return inw(addr);
}
#endif

#ifndef inl_p
#define inl_p inl_p
static inline u32 inl_p(unsigned long addr)
{
	return inl(addr);
}
#endif

#ifndef outb_p
#define outb_p outb_p
static inline void outb_p(u8 value, unsigned long addr)
{
	outb(value, addr);
}
#endif

#ifndef outw_p
#define outw_p outw_p
static inline void outw_p(u16 value, unsigned long addr)
{
	outw(value, addr);
}
#endif

#ifndef outl_p
#define outl_p outl_p
static inline void outl_p(u32 value, unsigned long addr)
{
	outl(value, addr);
}
#endif


/* insb, insw, insl, outsb, outsw, outsl removed - unused */

#ifdef __KERNEL__

#include <linux/vmalloc.h>
#define __io_virt(x) ((void __force *)(x))

#ifndef virt_to_phys
#define virt_to_phys virt_to_phys
static inline unsigned long virt_to_phys(volatile void *address)
{
	return __pa((unsigned long)address);
}
#endif

#ifndef phys_to_virt
#define phys_to_virt phys_to_virt
static inline void *phys_to_virt(unsigned long address)
{
	return __va(address);
}
#endif

#if   defined(CONFIG_GENERIC_IOREMAP)
#include <linux/pgtable.h>

void __iomem *ioremap_prot(phys_addr_t addr, size_t size, unsigned long prot);
void iounmap(volatile void __iomem *addr);

static inline void __iomem *ioremap(phys_addr_t addr, size_t size)
{
	 
	return ioremap_prot(addr, size, _PAGE_IOREMAP);
}
#endif  

#ifndef ioremap_wc
#define ioremap_wc ioremap
#endif

#ifndef ioremap_wt
#define ioremap_wt ioremap
#endif

#ifndef ioremap_uc
#define ioremap_uc ioremap_uc
static inline void __iomem *ioremap_uc(phys_addr_t offset, size_t size)
{
	return NULL;
}
#endif

#ifndef ioremap_np
#define ioremap_np ioremap_np
static inline void __iomem *ioremap_np(phys_addr_t offset, size_t size)
{
	return NULL;
}
#endif

extern void __iomem *ioport_map(unsigned long port, unsigned int nr);
extern void ioport_unmap(void __iomem *p);


#ifndef xlate_dev_mem_ptr
#define xlate_dev_mem_ptr xlate_dev_mem_ptr
static inline void *xlate_dev_mem_ptr(phys_addr_t addr)
{
	return __va(addr);
}
#endif

#ifndef unxlate_dev_mem_ptr
#define unxlate_dev_mem_ptr unxlate_dev_mem_ptr
static inline void unxlate_dev_mem_ptr(phys_addr_t phys, void *addr)
{
}
#endif

#ifndef virt_to_bus
static inline unsigned long virt_to_bus(void *address)
{
	return (unsigned long)address;
}

static inline void *bus_to_virt(unsigned long address)
{
	return (void *)address;
}
#endif

#ifndef memset_io
#define memset_io memset_io
static inline void memset_io(volatile void __iomem *addr, int value,
			     size_t size)
{
	memset(__io_virt(addr), value, size);
}
#endif

#ifndef memcpy_fromio
#define memcpy_fromio memcpy_fromio
static inline void memcpy_fromio(void *buffer,
				 const volatile void __iomem *addr,
				 size_t size)
{
	memcpy(buffer, __io_virt(addr), size);
}
#endif

#ifndef memcpy_toio
#define memcpy_toio memcpy_toio
static inline void memcpy_toio(volatile void __iomem *addr, const void *buffer,
			       size_t size)
{
	memcpy(__io_virt(addr), buffer, size);
}
#endif

#ifndef CONFIG_GENERIC_DEVMEM_IS_ALLOWED
extern int devmem_is_allowed(unsigned long pfn);
#endif

#endif  

#endif  
