 
#ifndef __X86_KERNEL_FPU_XSTATE_H
#define __X86_KERNEL_FPU_XSTATE_H

#include <asm/cpufeature.h>
#include <asm/fpu/xstate.h>

/* Inlined from asm/fpu/xcr.h */
/* XCR_XFEATURE_ENABLED_MASK, XCR_XFEATURE_IN_USE_MASK, xgetbv, xsetbv,
   xfeatures_in_use removed - unused */

/* xstate_init_xcomp_bv removed - inlined at single call site */

enum xstate_copy_mode {
	XSTATE_COPY_FP,
	XSTATE_COPY_FX,
	XSTATE_COPY_XSAVE,
};

/* struct membuf, __copy_xstate_to_uabi_buf, copy_xstate_to_uabi_buf,
 * copy_uabi_from_kernel_to_xstate, get_xsave_addr,
 * copy_sigframe_from_user_to_xstate, fpu__init_cpu_xstate,
 * fpu__init_system_xstate, xfeatures_mask_supervisor removed - unused */



#define REX_PREFIX

 
#define XSAVE		".byte " REX_PREFIX "0x0f,0xae,0x27"
#define XSAVEOPT	".byte " REX_PREFIX "0x0f,0xae,0x37"
#define XSAVEC		".byte " REX_PREFIX "0x0f,0xc7,0x27"
#define XSAVES		".byte " REX_PREFIX "0x0f,0xc7,0x2f"
#define XRSTOR		".byte " REX_PREFIX "0x0f,0xae,0x2f"
#define XRSTORS		".byte " REX_PREFIX "0x0f,0xc7,0x1f"

 
#define XSTATE_OP(op, st, lmask, hmask, err)				\
	asm volatile("1:" op "\n\t"					\
		     "xor %[err], %[err]\n"				\
		     "2:\n\t"						\
		     _ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_FAULT_MCE_SAFE)	\
		     : [err] "=a" (err)					\
		     : "D" (st), "m" (*st), "a" (lmask), "d" (hmask)	\
		     : "memory")

 
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

/* xfd_validate_state, xfd_update_state removed - empty stubs */

static inline void os_xsave(struct fpstate *fpstate)
{
	u64 mask = fpstate->xfeatures;
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err;

	WARN_ON_FPU(!alternatives_patched);
	/* xfd_validate_state removed - empty stub */
	XSTATE_XSAVE(&fpstate->regs.xsave, lmask, hmask, err);
	WARN_ON_FPU(err);
}

static inline void os_xrstor(struct fpstate *fpstate, u64 mask)
{
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	/* xfd_validate_state removed - empty stub */
	XSTATE_XRESTORE(&fpstate->regs.xsave, lmask, hmask);
}

/* os_xrstor_supervisor, xsave_to_user_sigframe, xrstor_from_user_sigframe,
   os_xrstor_safe removed - never called */


#endif
