 
#ifndef _ASM_X86_NMI_H
#define _ASM_X86_NMI_H

#include <asm/irq.h>
#include <asm/io.h>

enum {
	NMI_LOCAL=0,
	NMI_UNKNOWN,
	NMI_SERR,
	NMI_IO_CHECK,
	NMI_MAX
};

typedef int (*nmi_handler_t)(unsigned int, struct pt_regs *);

struct nmiaction {
	struct list_head	list;
	nmi_handler_t		handler;
	u64			max_duration;
	unsigned long		flags;
	const char		*name;
};

void local_touch_nmi(void);

#endif  
