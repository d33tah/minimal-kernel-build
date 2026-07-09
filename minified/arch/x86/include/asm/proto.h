 
#ifndef _ASM_X86_PROTO_H
#define _ASM_X86_PROTO_H

#include <asm/ldt.h>

void entry_INT80_32(void);
void entry_SYSENTER_32(void);
void __begin_SYSENTER_singlestep_region(void);
void __end_SYSENTER_singlestep_region(void);


#endif  
