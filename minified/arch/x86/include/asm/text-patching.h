 
#ifndef _ASM_X86_TEXT_PATCHING_H
#define _ASM_X86_TEXT_PATCHING_H

#include <linux/types.h>
#include <linux/stddef.h>
#include <asm/ptrace.h>

struct paravirt_patch_site;
#define __parainstructions	NULL
#define __parainstructions_end	NULL

extern void text_poke_early(void *addr, const void *opcode, size_t len);
extern void *text_poke(void *addr, const void *opcode, size_t len);
extern int poke_int3_handler(struct pt_regs *regs);
extern void text_poke_bp(void *addr, const void *opcode, size_t len, const void *emulate);
/* POKE_MAX_OPCODE_SIZE, INSN macros, text_opcode_size, text_gen_insn removed - unused */

extern int after_bootmem;
extern __ro_after_init struct mm_struct *poking_mm;
extern __ro_after_init unsigned long poking_addr;
/* int3_emulate_* functions removed - unused */

#endif  
