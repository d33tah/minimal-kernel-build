 
#ifndef _ASM_X86_MMU_CONTEXT_H
#define _ASM_X86_MMU_CONTEXT_H

#include <asm/desc.h>
#include <linux/atomic.h>
#include <linux/mm_types.h>
#define ARCH_DEFAULT_PKEY 0
#define execute_only_pkey(mm) (0)

#include <asm/tlbflush.h>
#include <asm/debugreg.h>

extern atomic64_t last_mm_ctx_id;

static inline void paravirt_activate_mm(struct mm_struct *prev,
					struct mm_struct *next)
{
}

static inline void load_mm_ldt(struct mm_struct *mm)
{
	clear_LDT();
}
static inline void switch_ldt(struct mm_struct *prev, struct mm_struct *next)
{
	DEBUG_LOCKS_WARN_ON(preemptible());
}

#define enter_lazy_tlb enter_lazy_tlb
extern void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk);

#define init_new_context init_new_context
static inline int init_new_context(struct task_struct *tsk,
				   struct mm_struct *mm)
{
	mutex_init(&mm->context.lock);

	mm->context.ctx_id = atomic64_inc_return(&last_mm_ctx_id);
	atomic64_set(&mm->context.tlb_gen, 0);

	return 0;
}

#define destroy_context destroy_context
static inline void destroy_context(struct mm_struct *mm)
{
}

extern void switch_mm(struct mm_struct *prev, struct mm_struct *next,
		      struct task_struct *tsk);

extern void switch_mm_irqs_off(struct mm_struct *prev, struct mm_struct *next,
			       struct task_struct *tsk);
#define switch_mm_irqs_off switch_mm_irqs_off

#define activate_mm(prev, next)			\
do {						\
	paravirt_activate_mm((prev), (next));	\
	switch_mm((prev), (next), NULL);	\
} while (0);

#define deactivate_mm(tsk, mm)			\
do {						\
	loadsegment(gs, 0);			\
} while (0)

static inline void arch_exit_mmap(struct mm_struct *mm)
{
}

#endif
