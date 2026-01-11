#ifndef __LINUX_KBUILD_H
#error "Please do not build this file directly, build asm-offsets.c instead"
#endif

#include <linux/efi.h>

#include <asm/ucontext.h>

void foo(void);

void foo(void)
{
	OFFSET(CPUINFO_x86, cpuinfo_x86, x86);
	OFFSET(CPUINFO_x86_vendor, cpuinfo_x86, x86_vendor);
	OFFSET(CPUINFO_x86_model, cpuinfo_x86, x86_model);
	OFFSET(CPUINFO_x86_stepping, cpuinfo_x86, x86_stepping);
	OFFSET(CPUINFO_cpuid_level, cpuinfo_x86, cpuid_level);
	OFFSET(CPUINFO_x86_capability, cpuinfo_x86, x86_capability);
	OFFSET(CPUINFO_x86_vendor_id, cpuinfo_x86, x86_vendor_id);
	BLANK();

	/* PT_EBX, PT_ECX, PT_EDX, PT_ESI, PT_EDI, PT_EBP, PT_DS, PT_ES removed - not used in assembly */
	OFFSET(PT_EAX, pt_regs, ax);
	OFFSET(PT_FS, pt_regs, fs);
	OFFSET(PT_GS, pt_regs, gs);
	OFFSET(PT_ORIG_EAX, pt_regs, orig_ax);
	OFFSET(PT_EIP, pt_regs, ip);
	OFFSET(PT_CS, pt_regs, cs);
	OFFSET(PT_EFLAGS, pt_regs, flags);
	OFFSET(PT_OLDESP, pt_regs, sp);
	OFFSET(PT_OLDSS, pt_regs, ss);
	BLANK();

	/* saved_context_gdt_desc removed - not used in assembly */

	DEFINE(TSS_entry2task_stack,
	       offsetof(struct cpu_entry_area, tss.x86_tss.sp1) -
		       offsetofend(struct cpu_entry_area,
				   entry_stack_page.stack));

	/* EFI_svam removed - not used */
}
