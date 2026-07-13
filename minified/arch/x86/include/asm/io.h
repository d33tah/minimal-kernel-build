 
#ifndef _ASM_X86_IO_H
#define _ASM_X86_IO_H

/* ARCH_HAS_IOREMAP_WC, ARCH_HAS_IOREMAP_WT removed - unused */

#include <linux/string.h>
#include <linux/compiler.h>
#include <asm/page.h>
#include <asm/early_ioremap.h>
#include <asm/pgtable_types.h>
#include <asm/shared/io.h>

/* build_mmio_write/writeb + readb/readw/readl/writew/writel removed - 0 callers */
/* __read, __write, _relaxed and __raw_ accessors removed - unused */
/* ARCH_HAS_VALID_PHYS_ADDR_RANGE, valid_*_range removed - unused */

 


 

static inline void *phys_to_virt(phys_addr_t address) {
	return __va(address); }

extern void native_io_delay(void);


static inline void slow_down_io(void) {
	native_io_delay(); }


#define BUILDIO(bwl, bw, type)						static inline void out##bwl##_p(type value, u16 port)			{										out##bwl(value, port);							slow_down_io();							}																		static inline type in##bwl##_p(u16 port)				{										type value = in##bwl(port);						slow_down_io();								return value;							}																		static inline void outs##bwl(u16 port, const void *addr, unsigned long count) {										asm volatile("rep; outs" #bwl							     : "+S"(addr), "+c"(count)						     : "d"(port) : "memory");				}																		static inline void ins##bwl(u16 port, void *addr, unsigned long count)	{										asm volatile("rep; ins" #bwl							     : "+D"(addr), "+c"(count)						     : "d"(port) : "memory");				}

BUILDIO(b, b, u8)
/* BUILDIO(w)/BUILDIO(l) removed - only generate 0-caller _p/outs/ins wrappers */
#undef BUILDIO


/* ioremap_change_attr, ioremap_wc, ioremap_wt removed - no callers */

/* ioport_map/unmap removed - declared but never defined/used */

/* ioremap_np removed - no callers */

/* phys_mem_access_encrypted removed - unused */

#endif  
