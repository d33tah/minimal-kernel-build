
#include <linux/sched.h>
#include <linux/stddef.h>
#include <linux/suspend.h>
#include <linux/kbuild.h>
#include <asm/processor.h>
#include <asm/thread_info.h>
#include <asm/sigframe.h>
#include <asm/bootparam.h>
/* --- 2025-12-07 20:29 --- Inlined suspend_32.h */
#include <asm/desc.h>
/* TDX not used in minimal kernel */


# include "asm-offsets_32.c"

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


	OFFSET(CPU_ENTRY_AREA_entry_stack, cpu_entry_area, entry_stack_page);
	DEFINE(SIZEOF_entry_stack, sizeof(struct entry_stack));
	DEFINE(MASK_entry_stack, (~(sizeof(struct entry_stack) - 1)));

	 
	OFFSET(TSS_sp0, tss_struct, x86_tss.sp0);
	OFFSET(TSS_sp1, tss_struct, x86_tss.sp1);

}
