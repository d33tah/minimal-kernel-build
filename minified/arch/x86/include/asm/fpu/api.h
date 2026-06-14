 
 

#ifndef _ASM_X86_FPU_API_H
#define _ASM_X86_FPU_API_H
#include <linux/bottom_half.h>

#include <asm/fpu/types.h>

 

 
#define KFPU_387	_BITUL(0)	 
#define KFPU_MXCSR	_BITUL(1)	 

extern void kernel_fpu_begin_mask(unsigned int kfpu_mask);
extern void kernel_fpu_end(void);
extern bool irq_fpu_usable(void);

 
static inline void kernel_fpu_begin(void)
{
	 
	kernel_fpu_begin_mask(KFPU_387 | KFPU_MXCSR);
}

 
static inline void fpregs_lock(void)
{
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		local_bh_disable();
	else
		preempt_disable();
}

static inline void fpregs_unlock(void)
{
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		local_bh_enable();
	else
		preempt_enable();
}

static inline void fpregs_assert_state_consistent(void) { }

 
extern void switch_fpu_return(void);


extern int  fpu__exception_code(struct fpu *fpu, int trap_nr);
extern void fpu_sync_fpstate(struct fpu *fpu);
extern void fpu_reset_from_exception_fixup(void);

 
extern void fpu__init_cpu(void);
extern void fpu__init_system(struct cpuinfo_x86 *c);
extern void fpu__init_check_bugs(void);
extern void fpu__resume_cpu(void);

static inline void fpstate_init_soft(struct swregs_state *soft) {}

 
DECLARE_PER_CPU(struct fpu *, fpu_fpregs_owner_ctx);

 
/* fpstate_free, fpu_update_guest_xfd, fpu_sync_guest_vmexit_xfd_state,
   fpstate_set_confidential, fpstate_is_confidential removed - unused */




extern long fpu_xstate_prctl(int option, unsigned long arg2);

#endif  
