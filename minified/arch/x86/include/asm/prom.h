 
 

#ifndef _ASM_X86_PROM_H
#define _ASM_X86_PROM_H
#ifndef __ASSEMBLY__

#include <linux/of.h>
#include <linux/types.h>
#include <linux/pci.h>

#include <asm/irq.h>
#include <linux/atomic.h>
#include <asm/setup.h>

#define of_ioapic 0

extern char cmd_line[COMMAND_LINE_SIZE];

#endif  
#endif
