#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/init_task.h>
#include <linux/fs.h>

#include <linux/uaccess.h>
#include <asm/processor.h>
#include <asm/desc.h>
#include <asm/traps.h>

/* ptr_ok removed - unused */
#define TSS(x) this_cpu_read(cpu_tss_rw.x86_tss.x)

static void set_df_gdt_entry(unsigned int cpu);

asmlinkage noinstr void __noreturn doublefault_shim(void)
{
	unsigned long cr2;
	struct pt_regs regs;

	BUILD_BUG_ON(sizeof(struct doublefault_stack) != PAGE_SIZE);

	cr2 = native_read_cr2();

	force_reload_TR();
	/* inlined set_df_gdt_entry */
	__set_tss_desc(
		smp_processor_id(), GDT_ENTRY_DOUBLEFAULT_TSS,
		&get_cpu_entry_area(smp_processor_id())->doublefault_stack.tss);

	regs.ss = TSS(ss);
	regs.__ssh = 0;
	regs.sp = TSS(sp);
	regs.flags = TSS(flags);
	regs.cs = TSS(cs);

	regs.__csh = 0;
	regs.ip = TSS(ip);
	regs.orig_ax = 0;
	regs.gs = TSS(gs);
	regs.__gsh = 0;
	regs.fs = TSS(fs);
	regs.__fsh = 0;
	regs.es = TSS(es);
	regs.__esh = 0;
	regs.ds = TSS(ds);
	regs.__dsh = 0;
	regs.ax = TSS(ax);
	regs.bp = TSS(bp);
	regs.di = TSS(di);
	regs.si = TSS(si);
	regs.dx = TSS(dx);
	regs.cx = TSS(cx);
	regs.bx = TSS(bx);

	exc_double_fault(&regs, 0, cr2);

	panic("cannot return from double fault\n");
}

/* Stubbed - doublefault_stack not needed for Hello World */
DEFINE_PER_CPU_PAGE_ALIGNED(struct doublefault_stack, doublefault_stack) = {};

/* set_df_gdt_entry inlined into doublefault_shim (~5 LOC) */

void doublefault_init_cpu_tss(void)
{
	/* Stubbed - double fault handling not needed for Hello World */
}
