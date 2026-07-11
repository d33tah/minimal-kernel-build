 
#ifndef __X86_KERNEL_FPU_INTERNAL_H
#define __X86_KERNEL_FPU_INTERNAL_H

extern struct fpstate init_fpstate;


static __always_inline __pure bool use_fxsr(void)
{
	return cpu_feature_enabled(X86_FEATURE_FXSR);
}


extern void fpstate_init_user(struct fpstate *fpstate);
extern void fpstate_reset(struct fpu *fpu);

#endif
