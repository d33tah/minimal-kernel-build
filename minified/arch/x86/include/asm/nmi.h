 
#ifndef _ASM_X86_NMI_H
#define _ASM_X86_NMI_H

#include <linux/irq_work.h>
#include <linux/pm.h>
#include <asm/irq.h>
#include <asm/io.h>


enum {
	NMI_LOCAL=0,
	NMI_UNKNOWN,
	NMI_SERR,
	NMI_IO_CHECK,
	NMI_MAX
};

void local_touch_nmi(void);

#endif  
