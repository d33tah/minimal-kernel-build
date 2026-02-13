 
#ifndef _ASM_X86_PROTO_H
#define _ASM_X86_PROTO_H

#ifndef LDT_ENTRY_SIZE
#define LDT_ENTRY_SIZE 8 /* inlined from asm/ldt.h */
#endif

void entry_INT80_32(void);

void x86_configure_nx(void);

#endif  
