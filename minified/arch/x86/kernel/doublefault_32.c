
#include <asm/traps.h>

#define TSS(x) this_cpu_read(cpu_tss_rw.x86_tss.x)

/* The double-fault task's stack and TSS (a task gate in the IDT uses it). */
struct doublefault_stack {
	unsigned long stack[(PAGE_SIZE - sizeof(struct x86_hw_tss)) /
			    sizeof(unsigned long)];
	struct x86_hw_tss tss;
} __aligned(PAGE_SIZE);

static void set_df_gdt_entry(unsigned int cpu);

asmlinkage noinstr void __noreturn doublefault_shim(void)
{
	unsigned long cr2;
	struct pt_regs regs;
	struct desc_struct *d;
	tss_desc tss;

	BUILD_BUG_ON(sizeof(struct doublefault_stack) != PAGE_SIZE);

	cr2 = native_read_cr2();

	/* force_reload_TR (folded from its sole caller) */
	d = get_current_gdt_rw();
	memcpy(&tss, &d[GDT_ENTRY_TSS], sizeof(tss_desc));
	tss.type = DESC_TSS;
	write_gdt_entry(d, GDT_ENTRY_TSS, &tss, DESC_TSS);
	load_TR_desc();

	set_df_gdt_entry(smp_processor_id());

	trace_hardirqs_off();

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

static DEFINE_PER_CPU_PAGE_ALIGNED(struct doublefault_stack, doublefault_stack) = { .tss = { .ldt		= 0, .io_bitmap_base	= IO_BITMAP_OFFSET_INVALID, .ip		= (unsigned long) asm_exc_double_fault, .flags		= X86_EFLAGS_FIXED, .es		= __USER_DS, .cs		= __KERNEL_CS, .ss		= __KERNEL_DS, .ds		= __USER_DS, .fs		= __KERNEL_PERCPU, .gs		= 0, .__cr3		= __pa_nodebug(swapper_pg_dir), }, };

static void set_df_gdt_entry(unsigned int cpu)
{
	__set_tss_desc(cpu, GDT_ENTRY_DOUBLEFAULT_TSS,
		       this_cpu_ptr(&doublefault_stack.tss));
}

void doublefault_init_cpu_tss(void)
{
	this_cpu_write(doublefault_stack.tss.sp,
		       (unsigned long)this_cpu_ptr(&doublefault_stack.stack) +
			       sizeof(doublefault_stack.stack));

	set_df_gdt_entry(smp_processor_id());
}
