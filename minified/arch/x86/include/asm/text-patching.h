
#ifndef _ASM_X86_TEXT_PATCHING_H
#define _ASM_X86_TEXT_PATCHING_H

#include <linux/types.h>
#include <linux/stddef.h>
#include <asm/ptrace.h>

extern int after_bootmem;
extern __ro_after_init struct mm_struct *poking_mm;
extern __ro_after_init unsigned long poking_addr;

#endif
