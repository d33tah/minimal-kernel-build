#ifndef _ASM_X86_MMU_CONTEXT_H
#define _ASM_X86_MMU_CONTEXT_H

#include <asm/desc.h>

#include <asm/tlbflush.h>
#include <asm/debugreg.h>

extern void switch_mm(struct mm_struct *prev, struct mm_struct *next,
		      struct task_struct *tsk);

extern void switch_mm_irqs_off(struct mm_struct *prev, struct mm_struct *next,
			       struct task_struct *tsk);

#define activate_mm(prev, next) switch_mm((prev), (next), NULL)

#define deactivate_mm(tsk, mm)      \
	do {                        \
		loadsegment(gs, 0); \
	} while (0)

#endif
