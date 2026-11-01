/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SPECIAL_INSNS_H
#define _ASM_X86_SPECIAL_INSNS_H

/*
 * The compiler should not reorder volatile asm statements with respect to each
 * other: they should execute in program order. However GCC 4.9.x and 5.x have
 * a bug (which was fixed in 8.1, 7.3 and 6.5) where they might reorder
 * volatile asm. The write functions are not affected since they have memory
 * clobbers preventing reordering. To prevent reads from being reordered with
 * respect to writes, use a dummy memory operand.
 */

#define __FORCE_ORDER "m"(*(unsigned int *)0x1000UL)

void native_write_cr0(unsigned long val);

static inline unsigned long native_read_cr0(void)
{
	unsigned long val;
	asm volatile("mov %%cr0,%0\n\t" : "=r"(val) : __FORCE_ORDER);
	return val;
}

static __always_inline unsigned long native_read_cr2(void)
{
	unsigned long val;
	asm volatile("mov %%cr2,%0\n\t" : "=r"(val) : __FORCE_ORDER);
	return val;
}

static __always_inline void native_write_cr2(unsigned long val)
{
	asm volatile("mov %0,%%cr2" : : "r"(val) : "memory");
}

static inline unsigned long __native_read_cr3(void)
{
	unsigned long val;
	asm volatile("mov %%cr3,%0\n\t" : "=r"(val) : __FORCE_ORDER);
	return val;
}

static inline void native_write_cr3(unsigned long val)
{
	asm volatile("mov %0,%%cr3" : : "r"(val) : "memory");
}

static inline unsigned long native_read_cr4(void)
{
	unsigned long val;

	/*
	 * This could fault if CR4 does not exist.  Non-existent CR4
	 * is functionally equivalent to CR4 == 0.  Keep it simple and pretend
	 * that CR4 == 0 on CPUs that don't have CR4.
	 */
	asm volatile("1: mov %%cr4, %0\n"
		     "2:\n" _ASM_EXTABLE(1b, 2b)
		     : "=r"(val)
		     : "0"(0), __FORCE_ORDER);
	return val;
}

void native_write_cr4(unsigned long val);

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

static inline void write_cr3(unsigned long x)
{
	native_write_cr3(x);
}

static inline void __write_cr4(unsigned long x)
{
	native_write_cr4(x);
}

#endif
