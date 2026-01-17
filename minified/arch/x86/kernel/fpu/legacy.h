 
#ifndef __X86_KERNEL_FPU_LEGACY_H
#define __X86_KERNEL_FPU_LEGACY_H

#include <asm/fpu/types.h>

extern unsigned int mxcsr_feature_mask;

static inline void ldmxcsr(u32 mxcsr)
{
	asm volatile("ldmxcsr %0" :: "m" (mxcsr));
}

#define kernel_insn(insn, output, input...)				\
	asm volatile("1:" #insn "\n\t"					\
		     "2:\n"						\
		     _ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_FPU_RESTORE)	\
		     : output : input)

/* user_insn, kernel_insn_err macros removed - only used by removed sigframe functions */
/* fnsave_to_user_sigframe, fxsave_to_user_sigframe, fxrstor_from_user_sigframe,
   frstor_from_user_sigframe removed - never called */

static inline void fxrstor(struct fxregs_state *fx)
{
	kernel_insn(fxrstor %[fx], "=m" (*fx), [fx] "m" (*fx));
}

static inline void frstor(struct fregs_state *fx)
{
	kernel_insn(frstor %[fx], "=m" (*fx), [fx] "m" (*fx));
}

static inline void fxsave(struct fxregs_state *fx)
{
	asm volatile( "fxsave %[fx]" : [fx] "=m" (*fx));
}

#endif
