// SPDX-License-Identifier: GPL-2.0
/*
 *  HW NMI watchdog support
 *
 *  started by Don Zickus, Copyright (C) 2010 Red Hat, Inc.
 *
 *  Arch specific calls to support NMI watchdog
 *
 *  Bits copied from original nmi.c file
 *
 */

#ifdef arch_trigger_cpumask_backtrace
static void nmi_raise_cpu_backtrace(cpumask_t *mask)
{
	apic->send_IPI_mask(mask, NMI_VECTOR);
}

void arch_trigger_cpumask_backtrace(const cpumask_t *mask, bool exclude_self)
{
	nmi_trigger_cpumask_backtrace(mask, exclude_self,
				      nmi_raise_cpu_backtrace);
}

static int nmi_cpu_backtrace_handler(unsigned int cmd, struct pt_regs *regs)
{
	if (nmi_cpu_backtrace(regs))
		return NMI_HANDLED;

	return NMI_DONE;
}
NOKPROBE_SYMBOL(nmi_cpu_backtrace_handler);

static int __init register_nmi_cpu_backtrace_handler(void)
{
	register_nmi_handler(NMI_LOCAL, nmi_cpu_backtrace_handler,
				0, "arch_bt");
	return 0;
}
early_initcall(register_nmi_cpu_backtrace_handler);
#endif
