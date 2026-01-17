 
#ifndef _ASM_X86_NMI_H
#define _ASM_X86_NMI_H

#include <linux/irq_work.h>
#include <linux/pm.h>
#include <asm/irq.h>
#include <asm/io.h>


#define NMI_FLAG_FIRST	1

enum {
	NMI_LOCAL=0,
	NMI_UNKNOWN,
	NMI_SERR,
	NMI_IO_CHECK,
	NMI_MAX
};

#define NMI_DONE	0
#define NMI_HANDLED	1

typedef int (*nmi_handler_t)(unsigned int, struct pt_regs *);

struct nmiaction {
	struct list_head	list;
	nmi_handler_t		handler;
	u64			max_duration;
	unsigned long		flags;
	const char		*name;
};

/* register_nmi_handler and __register_nmi_handler removed - never called */

void local_touch_nmi(void);

#endif  
