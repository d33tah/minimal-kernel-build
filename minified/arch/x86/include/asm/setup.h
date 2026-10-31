/* --- 2025-12-06 13:40 --- uapi/asm/setup.h removed (empty file) */
#ifndef _ASM_X86_SETUP_H
#define _ASM_X86_SETUP_H

#define COMMAND_LINE_SIZE 2048

#include <linux/pfn.h>
/*
 * Reserved space for vmalloc and iomap - defined in asm/page.h
 */
#define MAXMEM_PFN PFN_DOWN(MAXMEM)

/* 32-bit only kernel */

#define PARAM_SIZE 4096

#ifndef __ASSEMBLY__

/* __startup_64, startup_64_setup_env removed - 64-bit only, unused in 32-bit build */

extern struct boot_params boot_params;
extern char _text[];

extern unsigned long _brk_end;
void *extend_brk(size_t size, size_t align);

#define RESERVE_BRK(name, size)             \
	__section(".bss..brk") __aligned(1) \
		__used static char __brk_##name[size]

/* 32-bit only kernel */
asmlinkage void __init i386_start_kernel(void);

#else

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

#endif

#endif
