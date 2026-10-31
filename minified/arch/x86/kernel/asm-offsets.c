
#include <linux/sched.h>
#include <linux/kbuild.h>
#include <asm/ptrace.h>
/* --- 2025-12-07 20:29 --- Inlined suspend_32.h */
/* TDX not used in minimal kernel */

#include "asm-offsets_32.c"

static void __used common(void)
{
	BLANK();
	OFFSET(TASK_threadsp, task_struct, thread.sp);

	/* TDX offsets removed - not used in minimal kernel */

	BLANK();
	OFFSET(BP_scratch, boot_params, scratch);
	OFFSET(BP_init_size, boot_params, hdr.init_size);

	BLANK();
	DEFINE(PTREGS_SIZE, sizeof(struct pt_regs));
}
