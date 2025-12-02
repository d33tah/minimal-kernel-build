 
#ifndef _ASM_X86_SPECIAL_INSNS_H
#define _ASM_X86_SPECIAL_INSNS_H


#ifdef __KERNEL__

#include <asm/nops.h>
#include <asm/processor-flags.h>
#include <linux/irqflags.h>
#include <linux/jump_label.h>

 

#define __FORCE_ORDER "m"(*(unsigned int *)0x1000UL)

void native_write_cr0(unsigned long val);

static inline unsigned long native_read_cr0(void)
{
	unsigned long val;
	asm volatile("mov %%cr0,%0\n\t" : "=r" (val) : __FORCE_ORDER);
	return val;
}

static __always_inline unsigned long native_read_cr2(void)
{
	unsigned long val;
	asm volatile("mov %%cr2,%0\n\t" : "=r" (val) : __FORCE_ORDER);
	return val;
}

static __always_inline void native_write_cr2(unsigned long val)
{
	asm volatile("mov %0,%%cr2": : "r" (val) : "memory");
}

static inline unsigned long __native_read_cr3(void)
{
	unsigned long val;
	asm volatile("mov %%cr3,%0\n\t" : "=r" (val) : __FORCE_ORDER);
	return val;
}

static inline void native_write_cr3(unsigned long val)
{
	asm volatile("mov %0,%%cr3": : "r" (val) : "memory");
}

static inline unsigned long native_read_cr4(void)
{
	unsigned long val;
	 
	asm volatile("1: mov %%cr4, %0\n"
		     "2:\n"
		     _ASM_EXTABLE(1b, 2b)
		     : "=r" (val) : "0" (0), __FORCE_ORDER);
	return val;
}

void native_write_cr4(unsigned long val);

static inline u32 rdpkru(void)
{
	return 0;
}

static inline void wrpkru(u32 pkru)
{
}

static inline void native_wbinvd(void)
{
	asm volatile("wbinvd": : :"memory");
}

extern asmlinkage void asm_load_gs_index(unsigned int selector);

static inline void native_load_gs_index(unsigned int selector)
{
	unsigned long flags;

	local_irq_save(flags);
	asm_load_gs_index(selector);
	local_irq_restore(flags);
}

static inline unsigned long __read_cr4(void)
{
	return native_read_cr4();
}


static inline unsigned long read_cr0(void)
{
	return native_read_cr0();
}

static inline void write_cr0(unsigned long x)
{
	native_write_cr0(x);
}

static __always_inline unsigned long read_cr2(void)
{
	return native_read_cr2();
}

static __always_inline void write_cr2(unsigned long x)
{
	native_write_cr2(x);
}

 
static inline unsigned long __read_cr3(void)
{
	return __native_read_cr3();
}

static inline void write_cr3(unsigned long x)
{
	native_write_cr3(x);
}

static inline void __write_cr4(unsigned long x)
{
	native_write_cr4(x);
}

static inline void wbinvd(void)
{
	native_wbinvd();
}


static inline void load_gs_index(unsigned int selector)
{
	loadsegment(gs, selector);
}


static inline void clflush(volatile void *__p)
{
	asm volatile("clflush %0" : "+m" (*(volatile char __force *)__p));
}

static inline void clflushopt(volatile void *__p)
{
	alternative_io(".byte 0x3e; clflush %P0",
		       ".byte 0x66; clflush %P0",
		       X86_FEATURE_CLFLUSHOPT,
		       "+m" (*(volatile char __force *)__p));
}

static inline void clwb(volatile void *__p)
{
	volatile struct { char x[64]; } *p = __p;

	asm volatile(ALTERNATIVE_2(
		".byte 0x3e; clflush (%[pax])",
		".byte 0x66; clflush (%[pax])",  
		X86_FEATURE_CLFLUSHOPT,
		".byte 0x66, 0x0f, 0xae, 0x30",   
		X86_FEATURE_CLWB)
		: [p] "+m" (*p)
		: [pax] "a" (p));
}

#define nop() asm volatile ("nop")

/* Used by sync_core.h */
static inline void serialize(void)
{
	asm volatile(".byte 0xf, 0x1, 0xe8" ::: "memory");
}

/* Used by io.h */
static inline void movdir64b(void __iomem *dst, const void *src)
{
	const struct { char _[64]; } *__src = src;
	struct { char _[64]; } __iomem *__dst = dst;

	asm volatile(".byte 0x66, 0x0f, 0x38, 0xf8, 0x02"
		     : "+m" (*__dst)
		     :  "m" (*__src), "a" (__dst), "d" (__src));
}

#endif

#endif
