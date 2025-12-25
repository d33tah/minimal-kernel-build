
#ifndef _ASM_X86_NOPS_H
#define _ASM_X86_NOPS_H

#include <asm/asm.h>

#define ASM_NOP_MAX 8

#ifndef __ASSEMBLY__
extern const unsigned char * const x86_nops[];
#endif

#endif
