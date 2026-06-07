 
#ifndef _ASM_X86_FPU_API_H
#define _ASM_X86_FPU_API_H
#include <linux/bottom_half.h>

#include <asm/fpu/types.h>

/* CONFIG_PREEMPT_RT not enabled */
static inline void fpregs_lock(void)
{
	local_bh_disable();
}

static inline void fpregs_unlock(void)
{
	local_bh_enable();
}
extern void switch_fpu_return(void);

extern void fpu_reset_from_exception_fixup(void);

extern void fpu__init_cpu(void);
extern void fpu__init_system(struct cpuinfo_x86 *c);

DECLARE_PER_CPU(struct fpu *, fpu_fpregs_owner_ctx);

#endif  
