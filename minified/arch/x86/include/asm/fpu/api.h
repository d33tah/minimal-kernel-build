 
 

#ifndef _ASM_X86_FPU_API_H
#define _ASM_X86_FPU_API_H
#include <linux/bottom_half.h>

#include <asm/fpu/types.h>

 

 
/* KFPU_387, KFPU_MXCSR removed - only used by removed kernel_fpu_begin_mask */
/* kernel_fpu_begin_mask, kernel_fpu_end, irq_fpu_usable, fpregs_mark_activate removed - never called */

/* CONFIG_PREEMPT_RT not enabled */
static inline void fpregs_lock(void)
{
	local_bh_disable();
}

static inline void fpregs_unlock(void)
{
	local_bh_enable();
}
/* fpregs_assert_state_consistent removed - empty stub */
extern void switch_fpu_return(void);

 
/* cpu_has_xfeatures removed - never called */

extern int  fpu__exception_code(struct fpu *fpu, int trap_nr);
extern void fpu_sync_fpstate(struct fpu *fpu);
extern void fpu_reset_from_exception_fixup(void);

extern void fpu__init_cpu(void);
extern void fpu__init_system(struct cpuinfo_x86 *c);
/* fpu__init_check_bugs, fpu__resume_cpu removed - empty/never called */

DECLARE_PER_CPU(struct fpu *, fpu_fpregs_owner_ctx);

/* KVM guest FPU functions removed - unused (no KVM support):
   fpstate_free, fpu_update_guest_xfd, fpu_sync_guest_vmexit_xfd_state,
   fpstate_set_confidential, fpstate_is_confidential,
   fpstate_clear_xstate_component, xstate_get_guest_group_perm,
   fpu_alloc_guest_fpstate, fpu_free_guest_fpstate, fpu_swap_kvm_fpstate,
   fpu_enable_guest_xfd_features, fpu_copy_guest_fpstate_to_uabi,
   fpu_copy_uabi_to_guest_fpstate, fpu_xstate_prctl */

#endif  
