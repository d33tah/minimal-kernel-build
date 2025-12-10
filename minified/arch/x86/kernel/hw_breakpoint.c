/* Stub hardware breakpoints */
#include <linux/hw_breakpoint.h>
#include <linux/percpu.h>

DEFINE_PER_CPU(unsigned long, cpu_dr7);

/* hw_breakpoint_restore, arch_install_hw_breakpoint, arch_uninstall_hw_breakpoint,
   hw_breakpoint_arch_parse, encode_dr7, decode_dr7, arch_bp_generic_fields
   removed - unused */

void flush_ptrace_hw_breakpoint(struct task_struct *tsk)
{
}
