// FPU signal handling - heavily stubbed version
// Original: ~396 LOC, current: ~22 LOC
// Most functions removed since signals/sigframes are not used in this minimal build

#include <linux/cpu.h>
#include <linux/pagemap.h>

#include <asm/fpu/signal.h>
#include <asm/fpu/xstate.h>

#include <asm/sigframe.h>
#include "internal.h"
#include "xstate.h"

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
