// SPDX-License-Identifier: GPL-2.0
/*
 *  NMI backtrace support
 *
 * Gratuitously copied from arch/x86/kernel/apic/hw_nmi.c by Russell King,
 * with the following header:
 *
 *  HW NMI watchdog support
 *
 *  started by Don Zickus, Copyright (C) 2010 Red Hat, Inc.
 *
 *  Arch specific calls to support NMI watchdog
 *
 *  Bits copied from original nmi.c file
 */
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/kprobes.h>
#include <linux/nmi.h>
#include <linux/cpu.h>
#include <linux/sched/debug.h>

#ifdef arch_trigger_cpumask_backtrace
/* For reliability, we're prepared to waste bits here. */
static DECLARE_BITMAP(backtrace_mask, NR_CPUS) __read_mostly;

/* "in progress" flag of arch_trigger_cpumask_backtrace */
static unsigned long backtrace_flag;

/*
 * When raise() is called it will be passed a pointer to the
 * backtrace_mask. Architectures that call nmi_cpu_backtrace()
 * directly from their raise() functions may rely on the mask
 * they are passed being updated as a side effect of this call.
 */
void nmi_trigger_cpumask_backtrace(const cpumask_t *mask,
				   bool exclude_self,
				   void (*raise)(cpumask_t *mask))
{
	/* Stubbed for minification */
}

// Dump stacks even for idle CPUs.
static bool backtrace_idle;
module_param(backtrace_idle, bool, 0644);

bool nmi_cpu_backtrace(struct pt_regs *regs)
{
	/* Stubbed for minification */
	return false;
}
NOKPROBE_SYMBOL(nmi_cpu_backtrace);
#endif
