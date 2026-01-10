 
 

#ifndef _ASM_X86_CLOCKSOURCE_H
#define _ASM_X86_CLOCKSOURCE_H

#include <asm/vdso/clocksource.h>

extern unsigned int vclocks_used;

static inline void vclocks_set_used(unsigned int which)
{
	WRITE_ONCE(vclocks_used, READ_ONCE(vclocks_used) | (1 << which));
}

#endif  
