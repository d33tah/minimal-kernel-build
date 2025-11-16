 
#ifndef _ASM_IRQ_WORK_H
#define _ASM_IRQ_WORK_H

#include <asm/cpufeature.h>

static inline bool arch_irq_work_has_interrupt(void)
{
	return false;
}

#endif  
