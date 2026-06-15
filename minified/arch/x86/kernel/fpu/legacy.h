 
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

static inline void fxrstor(struct fxregs_state *fx)
{
	if (IS_ENABLED(CONFIG_X86_32))
		kernel_insn(fxrstor %[fx], "=m" (*fx), [fx] "m" (*fx));
	else
		kernel_insn(fxrstorq %[fx], "=m" (*fx), [fx] "m" (*fx));
}

static inline void frstor(struct fregs_state *fx)
{
	kernel_insn(frstor %[fx], "=m" (*fx), [fx] "m" (*fx));
}

static inline void fxsave(struct fxregs_state *fx)
{
	if (IS_ENABLED(CONFIG_X86_32))
		asm volatile( "fxsave %[fx]" : [fx] "=m" (*fx));
	else
		asm volatile("fxsaveq %[fx]" : [fx] "=m" (*fx));
}

#endif
