/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SETUP_H
#define _ASM_X86_SETUP_H

#define COMMAND_LINE_SIZE 2048

#include <linux/pfn.h>
/*
 * Reserved space for vmalloc and iomap - defined in asm/page.h
 */
#define MAXMEM_PFN PFN_DOWN(MAXMEM)

/* 32-bit only kernel */

#define PARAM_SIZE 4096 /* sizeof(struct boot_params) */

#ifndef __ASSEMBLY__

/*
 * This is set up by the setup-routine at boot-time
 */
extern struct boot_params boot_params;
extern char _text[];

/* exceedingly early brk-like allocator */
extern unsigned long _brk_end;
void *extend_brk(size_t size, size_t align);

/*
 * Reserve space in the .brk section, which is a block of memory from which the
 * caller is allowed to allocate very early (before even memblock is available)
 * by calling extend_brk().  All allocated memory will be eventually converted
 * to memblock.  Any leftover unallocated memory will be freed.
 *
 * The size is in bytes.
 */
#define RESERVE_BRK(name, size)             \
	__section(".bss..brk") __aligned(1) \
		__used static char __brk_##name[size]

/* 32-bit only kernel */
asmlinkage void __init i386_start_kernel(void);

#else /* __ASSEMBLY */

/* clang-format off */
.macro __RESERVE_BRK name, size
	.pushsection .bss..brk, "aw"
SYM_DATA_START(__brk_\name)
	.skip \size
SYM_DATA_END(__brk_\name)
	.popsection
.endm
/* clang-format on */

#define RESERVE_BRK(name, size) __RESERVE_BRK name, size

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_SETUP_H */
