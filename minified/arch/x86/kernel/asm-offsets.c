#include <linux/sched.h>
#include <linux/kbuild.h>
#include <asm/ptrace.h>

#include "asm-offsets_32.c"

static void __used common(void)
{
	BLANK();
	OFFSET(TASK_threadsp, task_struct, thread.sp);

	BLANK();
	OFFSET(BP_scratch, boot_params, scratch);
	OFFSET(BP_init_size, boot_params, hdr.init_size);

	BLANK();
	DEFINE(PTREGS_SIZE, sizeof(struct pt_regs));
}
