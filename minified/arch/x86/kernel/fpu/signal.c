// FPU signal handling - heavily stubbed version
// Original: ~396 LOC, current: ~22 LOC
// Most functions removed since signals/sigframes are not used in this minimal build

#include <linux/cpu.h>
#include <linux/pagemap.h>

#include <asm/fpu/signal.h>
/* asm/fpu/regset.h removed - not used */
#include <asm/fpu/xstate.h>

#include <asm/sigframe.h>
/* asm/trapnr.h removed - no X86_TRAP_ constants used */
/* asm/trace/fpu.h removed - all trace stubs empty */
/* context.h removed - no functions from it used */
#include "internal.h"
/* legacy.h removed - nothing from it used here */
#include "xstate.h"

/* copy_fpstate_to_sigframe, fpu__restore_sig, fpu__alloc_mathframe removed - never called */
/* All static helpers also removed: check_xstate_in_sigframe, save_fsave_header,
   save_sw_bytes, save_xstate_epilog, copy_fpregs_to_sigframe,
   __restore_fpregs_from_user, restore_fpregs_from_user, __fpu_restore_sig,
   xstate_sigframe_size */

unsigned long __init fpu__get_fpstate_size(void)
{
	unsigned long ret = fpu_user_cfg.max_size;

	if (use_xsave())
		ret += FP_XSTATE_MAGIC2_SIZE;

	/* X86_32 always enabled */
	if (use_fxsr())
		ret += sizeof(struct fregs_state);

	return ret;
}
