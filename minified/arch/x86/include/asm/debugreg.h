 
#ifndef _ASM_X86_DEBUGREG_H
#define _ASM_X86_DEBUGREG_H

#include <linux/bug.h>
#define DR6_RESERVED	(0xFFFF0FF0)
#define set_debugreg(value, register)				\
	native_set_debugreg(register, value)

static __always_inline void native_set_debugreg(int regno, unsigned long value)
{
	switch (regno) {
	case 0:
		asm("mov %0, %%db0"	::"r" (value));
		break;
	case 1:
		asm("mov %0, %%db1"	::"r" (value));
		break;
	case 2:
		asm("mov %0, %%db2"	::"r" (value));
		break;
	case 3:
		asm("mov %0, %%db3"	::"r" (value));
		break;
	case 6:
		asm("mov %0, %%db6"	::"r" (value));
		break;
	case 7:
		asm("mov %0, %%db7"	::"r" (value));
		break;
	default:
		BUG();
	}
}

#endif
