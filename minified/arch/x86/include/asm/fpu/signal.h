 
 
#ifndef _ASM_X86_FPU_SIGNAL_H
#define _ASM_X86_FPU_SIGNAL_H

#include <linux/compat.h>
#include <asm/user.h> /* linux/user.h redirect */

#include <asm/fpu/types.h>

# define user_i387_ia32_struct	user_i387_struct
# define user32_fxsr_struct	user_fxsr_struct
# define ia32_setup_frame	__setup_frame
# define ia32_setup_rt_frame	__setup_rt_frame

/* convert_from_fxsr, convert_to_fxsr removed - only caller was signal.c which is now stubbed */

/* fpu__alloc_mathframe removed - never called */

unsigned long fpu__get_fpstate_size(void);

/* copy_fpstate_to_sigframe, fpu__restore_sig, fpu__clear_user_states removed - never called */

extern void restore_fpregs_from_fpstate(struct fpstate *fpstate, u64 mask);
#endif  
