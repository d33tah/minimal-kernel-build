// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Minimal stub - hardware breakpoints not needed for minimal kernel
 */
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <linux/percpu.h>
#include <linux/kernel.h>
#include <linux/export.h>

/* Per cpu debug control register value */
DEFINE_PER_CPU(unsigned long, cpu_dr7);
EXPORT_PER_CPU_SYMBOL(cpu_dr7);

void hw_breakpoint_restore(void)
{
	/* Stubbed */
}
EXPORT_SYMBOL_GPL(hw_breakpoint_restore);

int arch_install_hw_breakpoint(struct perf_event *bp)
{
	return -ENOSYS;
}

void arch_uninstall_hw_breakpoint(struct perf_event *bp)
{
}

int hw_breakpoint_arch_parse(struct perf_event *bp,
			      const struct perf_event_attr *attr,
			      struct arch_hw_breakpoint *hw)
{
	return -ENOSYS;
}

void flush_ptrace_hw_breakpoint(struct task_struct *tsk)
{
}

unsigned long encode_dr7(int drnum, unsigned int len, unsigned int type)
{
	return 0;
}

int decode_dr7(unsigned long dr7, int bpnum, unsigned *len, unsigned *type)
{
	return 0;
}

int arch_bp_generic_fields(int x86_len, int x86_type,
			    int *gen_len, int *gen_type)
{
	return -EINVAL;
}
