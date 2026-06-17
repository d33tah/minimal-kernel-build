 
#ifndef _ASM_X86_NMI_H
#define _ASM_X86_NMI_H

#include <linux/irq_work.h>
#include <linux/pm.h>
#include <asm/irq.h>
#include <asm/io.h>

void local_touch_nmi(void);

#endif  
