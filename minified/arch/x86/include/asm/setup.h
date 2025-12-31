/* --- 2025-12-06 13:40 --- uapi/asm/setup.h removed (empty file) */
#ifndef _ASM_X86_SETUP_H
#define _ASM_X86_SETUP_H

#define COMMAND_LINE_SIZE 256  /* Reduced from 2048 for minimal boot */

#include <linux/linkage.h>
#include <asm/page_types.h>
#include <asm/ibt.h>

/* 32-bit only kernel */
#include <linux/pfn.h>

#define MAXMEM_PFN	PFN_DOWN(MAXMEM)
#define MAX_NONPAE_PFN	(1 << 20)  

#define PARAM_SIZE 4096		 

#define OLD_CL_MAGIC		0xA33F
#define OLD_CL_ADDRESS		0x020	 
#define NEW_CL_POINTER		0x228	 

#ifndef __ASSEMBLY__
#include <asm/bootparam.h>
#include <asm/x86_init.h>

extern u64 relocated_ramdisk;

struct pt_regs;

void early_platform_quirks(void);

/* saved_video_mode removed - set but never read */

extern void reserve_standard_io_resources(void);
extern void i386_reserve_resources(void);
/* x86_intel_mid_early_setup, x86_ce4100_early_setup removed - empty stubs */
#ifndef _SETUP

#include <linux/kernel.h>

extern struct boot_params boot_params;
extern char _text[];

/* kaslr_enabled, kaslr_memory_enabled, kaslr_offset removed - unused */

#define LOWMEMSIZE()	(0x9f000)

 
extern unsigned long _brk_end;
void *extend_brk(size_t size, size_t align);

 
#define RESERVE_BRK(name, size)					\
	__section(".bss..brk") __aligned(1) __used	\
	static char __brk_##name[size]

extern void probe_roms(void);

void clear_bss(void);

/* 32-bit only kernel */
asmlinkage void __init i386_start_kernel(void);

#endif  

#else   

.macro __RESERVE_BRK name, size
	.pushsection .bss..brk, "aw"
SYM_DATA_START(__brk_\name)
	.skip \size
SYM_DATA_END(__brk_\name)
	.popsection
.endm

#define RESERVE_BRK(name, size) __RESERVE_BRK name, size

#endif  

#endif  
