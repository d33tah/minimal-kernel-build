#ifndef __LINUX_KBUILD_H
#error "Please do not build this file directly, build asm-offsets.c instead"
#endif

void foo(void)
{
	OFFSET(PT_EAX, pt_regs, ax);
	OFFSET(PT_FS, pt_regs, fs);
	OFFSET(PT_GS, pt_regs, gs);
	OFFSET(PT_ORIG_EAX, pt_regs, orig_ax);
	OFFSET(PT_EIP, pt_regs, ip);
	OFFSET(PT_CS, pt_regs, cs);
	OFFSET(PT_EFLAGS, pt_regs, flags);
	OFFSET(PT_OLDESP, pt_regs, sp);
	BLANK();

	BLANK();

	DEFINE(TSS_entry2task_stack,
	       offsetof(struct cpu_entry_area, tss.x86_tss.sp1) -
		       offsetofend(struct cpu_entry_area,
				   entry_stack_page.stack));

	BLANK();
}
