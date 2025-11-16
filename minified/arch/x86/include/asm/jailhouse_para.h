 

 

#ifndef _ASM_X86_JAILHOUSE_PARA_H
#define _ASM_X86_JAILHOUSE_PARA_H

#include <linux/types.h>

static inline bool jailhouse_paravirt(void)
{
	return false;
}

#endif  
