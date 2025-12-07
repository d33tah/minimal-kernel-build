 
#ifndef _ASM_X86_MODULE_H
#define _ASM_X86_MODULE_H

/* Inlined from asm-generic/module.h */
#define Elf_Shdr	Elf32_Shdr
#define Elf_Phdr	Elf32_Phdr
#define Elf_Sym		Elf32_Sym
#define Elf_Dyn		Elf32_Dyn
#define Elf_Ehdr	Elf32_Ehdr
#define Elf_Addr	Elf32_Addr
#define Elf_Rel		Elf32_Rel
#define ELF_R_TYPE(X)	ELF32_R_TYPE(X)
#define ELF_R_SYM(X)	ELF32_R_SYM(X)

#include <asm/orc_types.h>

struct mod_arch_specific {
};

#endif  
