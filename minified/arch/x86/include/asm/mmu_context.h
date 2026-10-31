
#ifndef _ASM_X86_MMU_CONTEXT_H
#define _ASM_X86_MMU_CONTEXT_H

#include <asm/desc.h>

#include <asm/tlbflush.h>
#include <asm/debugreg.h>

extern atomic64_t last_mm_ctx_id;

extern void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk);

static inline int init_new_context(struct task_struct *tsk,
				   struct mm_struct *mm)
{
	mm->context.ctx_id = atomic64_inc_return(&last_mm_ctx_id);
	atomic64_set(&mm->context.tlb_gen, 0);

	return 0;
}

extern void switch_mm(struct mm_struct *prev, struct mm_struct *next,
		      struct task_struct *tsk);

extern void switch_mm_irqs_off(struct mm_struct *prev, struct mm_struct *next,
			       struct task_struct *tsk);

#define activate_mm(prev, next) switch_mm((prev), (next), NULL)

#define deactivate_mm(tsk, mm)      \
	do {                        \
		loadsegment(gs, 0); \
	} while (0)

/* arch_dup_pkeys, arch_dup_mmap, arch_exit_mmap removed - unused (no-op) */

/* is_64bit_mm removed - unused */

/* arch_vma_access_permitted removed - constant-true (no PKU), 0 callers */

#endif
