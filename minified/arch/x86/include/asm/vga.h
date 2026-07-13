 
 

#ifndef _ASM_X86_VGA_H
#define _ASM_X86_VGA_H

#include <asm/set_memory.h>

 

#define VGA_MAP_MEM(x, s)					({									unsigned long start = (unsigned long)phys_to_virt(x);										start;							})

#endif  
