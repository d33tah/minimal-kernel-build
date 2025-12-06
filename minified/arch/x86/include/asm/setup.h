/* --- 2025-12-06 13:40 --- uapi/asm/setup.h removed (empty file) */
#ifndef _ASM_X86_SETUP_H
#define _ASM_X86_SETUP_H

#define COMMAND_LINE_SIZE 2048

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

 
static inline void vsmp_init(void) { }

struct pt_regs;

void setup_bios_corruption_check(void);
void early_platform_quirks(void);

extern unsigned long saved_video_mode;

extern void reserve_standard_io_resources(void);
extern void i386_reserve_resources(void);
extern unsigned long __startup_64(unsigned long physaddr, struct boot_params *bp);
extern void startup_64_setup_env(unsigned long physbase);
extern void early_setup_idt(void);
extern void __init do_early_exception(struct pt_regs *regs, int trapnr);

static inline void x86_intel_mid_early_setup(void) { }

static inline void x86_ce4100_early_setup(void) { }

#ifndef _SETUP

#include <linux/kernel.h>

 
extern struct boot_params boot_params;
extern char _text[];

static inline bool kaslr_enabled(void)
{
	return IS_ENABLED(CONFIG_RANDOMIZE_MEMORY) &&
		!!(boot_params.hdr.loadflags & KASLR_FLAG);
}

 
static inline bool kaslr_memory_enabled(void)
{
	return kaslr_enabled() && !IS_ENABLED(CONFIG_KASAN);
}

static inline unsigned long kaslr_offset(void)
{
	return (unsigned long)&_text - __START_KERNEL;
}

 
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
