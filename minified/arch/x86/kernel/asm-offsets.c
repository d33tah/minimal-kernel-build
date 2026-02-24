#include <linux/sched.h>
#include <linux/stddef.h>
#include <linux/hardirq.h>
#define DEFINE(sym, val) \
	asm volatile("\n.ascii \"->" #sym " %0 " #val "\"" : : "i"(val))
#define BLANK() asm volatile("\n.ascii \"->\"" : :)
#define OFFSET(sym, str, mem) DEFINE(sym, offsetof(struct str, mem))
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>
#include <asm/bootparam.h>
#include <asm/desc.h>

#include "asm-offsets_32.c"

static void __used common(void)
{
	BLANK();
	OFFSET(TASK_threadsp, task_struct, thread.sp);

	BLANK();
	OFFSET(BP_scratch, boot_params, scratch);
	OFFSET(BP_kernel_alignment, boot_params, hdr.kernel_alignment);
	OFFSET(BP_init_size, boot_params, hdr.init_size);

	BLANK();
	DEFINE(PTREGS_SIZE, sizeof(struct pt_regs));

	OFFSET(CPU_ENTRY_AREA_entry_stack, cpu_entry_area, entry_stack_page);
	DEFINE(SIZEOF_entry_stack, sizeof(struct entry_stack));
	DEFINE(MASK_entry_stack, (~(sizeof(struct entry_stack) - 1)));

	OFFSET(TSS_sp0, tss_struct, x86_tss.sp0);
	OFFSET(TSS_sp1, tss_struct, x86_tss.sp1);
}
