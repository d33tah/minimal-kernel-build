/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SEGMENT_H
#define _ASM_X86_SEGMENT_H

#include <asm/ibt.h>

/*
 * Constructor for a conventional segment GDT (or LDT) entry.
 * This is a macro so it can be used in initializers.
 */
#define GDT_ENTRY(flags, base, limit)                      \
	((((base) & _AC(0xff000000, ULL)) << (56 - 24)) |  \
	 (((flags) & _AC(0x0000f0ff, ULL)) << 40) |        \
	 (((limit) & _AC(0x000f0000, ULL)) << (48 - 16)) | \
	 (((base) & _AC(0x00ffffff, ULL)) << 16) |         \
	 (((limit) & _AC(0x0000ffff, ULL))))

/* Simple and small GDT entries for booting only: */

#define GDT_ENTRY_BOOT_CS 2
#define GDT_ENTRY_BOOT_DS 3
#define GDT_ENTRY_BOOT_TSS 4
#define __BOOT_CS (GDT_ENTRY_BOOT_CS * 8)
#define __BOOT_DS (GDT_ENTRY_BOOT_DS * 8)
#define __BOOT_TSS (GDT_ENTRY_BOOT_TSS * 8)

/*
 * Bottom two bits of selector give the ring
 * privilege level
 */
#define SEGMENT_RPL_MASK 0x3

/*
 * When running on Xen PV, the actual privilege level of the kernel is 1,
 * not 0. Testing the Requested Privilege Level in a segment selector to
 * determine whether the context is user mode or kernel mode with
 * SEGMENT_RPL_MASK is wrong because the PV kernel's privilege level
 * matches the 0x3 mask.
 *
 * Testing with USER_SEGMENT_RPL_MASK is valid for both native and Xen PV
 * kernels because privilege level 2 is never used.
 */
#define USER_SEGMENT_RPL_MASK 0x2

/* User mode is privilege level 3: */
#define USER_RPL 0x3

#define GDT_ENTRY_KERNEL_CS 12
#define GDT_ENTRY_KERNEL_DS 13
#define GDT_ENTRY_DEFAULT_USER_CS 14
#define GDT_ENTRY_DEFAULT_USER_DS 15
#define GDT_ENTRY_TSS 16

#define GDT_ENTRY_DOUBLEFAULT_TSS 31

/*
 * Number of entries in the GDT table:
 */
#define GDT_ENTRIES 32

/*
 * Segment selector values corresponding to the above entries:
 */

#define __KERNEL_CS (GDT_ENTRY_KERNEL_CS * 8)
#define __KERNEL_DS (GDT_ENTRY_KERNEL_DS * 8)
#define __USER_DS (GDT_ENTRY_DEFAULT_USER_DS * 8 + 3)
#define __USER_CS (GDT_ENTRY_DEFAULT_USER_CS * 8 + 3)

#define __KERNEL_PERCPU 0

#define IDT_ENTRIES 256
#define NUM_EXCEPTION_VECTORS 32

/* Bitmask of exception vectors which push an error code on the stack: */
#define EXCEPTION_ERRCODE_MASK 0x20027d00

#define GDT_SIZE (GDT_ENTRIES * 8)

#define EARLY_IDT_HANDLER_SIZE (9 + ENDBR_INSN_SIZE)

#ifndef __ASSEMBLY__

extern const char early_idt_handler_array[NUM_EXCEPTION_VECTORS]
					 [EARLY_IDT_HANDLER_SIZE];
extern void early_ignore_irq(void);

/* clang-format off */
#define __loadsegment_simple(seg, value)				\
do {									\
	unsigned short __val = (value);					\
									\
	asm volatile("						\n"	\
		     "1:	movl %k0,%%" #seg "		\n"	\
		     _ASM_EXTABLE_TYPE_REG(1b, 1b, EX_TYPE_ZERO_REG, %k0)\
		     : "+r" (__val) : : "memory");			\
} while (0)
/* clang-format on */

#define __loadsegment_fs(value) __loadsegment_simple(fs, (value))
#define __loadsegment_gs(value) __loadsegment_simple(gs, (value))

#define loadsegment(seg, value) __loadsegment_##seg(value)

/*
 * Save a segment register away:
 */
#define savesegment(seg, value) \
	asm("mov %%" #seg ",%0" : "=r"(value) : : "memory")

#endif

#endif
