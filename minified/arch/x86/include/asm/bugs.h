 
#ifndef _ASM_X86_BUGS_H
#define _ASM_X86_BUGS_H

#include <asm/processor.h>

extern void check_bugs(void);

int ppro_with_ram_bug(void);

extern void cpu_bugs_smt_update(void);

#endif  
