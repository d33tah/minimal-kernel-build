 
#ifndef __X86_KERNEL_FPU_XSTATE_H
#define __X86_KERNEL_FPU_XSTATE_H

#include <asm/cpufeature.h>
#include <asm/fpu/xstate.h>

#define REX_PREFIX

#define XSAVE		".byte " REX_PREFIX "0x0f,0xae,0x27"
#define XSAVEOPT	".byte " REX_PREFIX "0x0f,0xae,0x37"
#define XSAVEC		".byte " REX_PREFIX "0x0f,0xc7,0x27"
#define XSAVES		".byte " REX_PREFIX "0x0f,0xc7,0x2f"
#define XRSTOR		".byte " REX_PREFIX "0x0f,0xae,0x2f"
#define XRSTORS		".byte " REX_PREFIX "0x0f,0xc7,0x1f"

#define XSTATE_XSAVE(st, lmask, hmask, err)				\
	asm volatile(ALTERNATIVE_3(XSAVE,				\
				   XSAVEOPT, X86_FEATURE_XSAVEOPT,	\
				   XSAVEC,   X86_FEATURE_XSAVEC,	\
				   XSAVES,   X86_FEATURE_XSAVES)	\
		     "\n"						\
		     "xor %[err], %[err]\n"				\
		     "3:\n"						\
		     _ASM_EXTABLE_TYPE_REG(661b, 3b, EX_TYPE_EFAULT_REG, %[err]) \
		     : [err] "=r" (err)					\
		     : "D" (st), "m" (*st), "a" (lmask), "d" (hmask)	\
		     : "memory")

#define XSTATE_XRESTORE(st, lmask, hmask)				\
	asm volatile(ALTERNATIVE(XRSTOR,				\
				 XRSTORS, X86_FEATURE_XSAVES)		\
		     "\n"						\
		     "3:\n"						\
		     _ASM_EXTABLE_TYPE(661b, 3b, EX_TYPE_FPU_RESTORE)	\
		     :							\
		     : "D" (st), "m" (*st), "a" (lmask), "d" (hmask)	\
		     : "memory")

static inline void os_xsave(struct fpstate *fpstate)
{
	u64 mask = fpstate->xfeatures;
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err;

	WARN_ON_FPU(!alternatives_patched);
	XSTATE_XSAVE(&fpstate->regs.xsave, lmask, hmask, err);
	WARN_ON_FPU(err);
}

static inline void os_xrstor(struct fpstate *fpstate, u64 mask)
{
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	XSTATE_XRESTORE(&fpstate->regs.xsave, lmask, hmask);
}

#endif
