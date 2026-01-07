 
#ifndef _ASM_X86_REBOOT_H
#define _ASM_X86_REBOOT_H

#include <linux/kdebug.h>

struct pt_regs;

/* struct machine_ops removed - unused */

/* native_machine_crash_shutdown removed - declared but never implemented */
/* native_machine_shutdown removed - never called */
/* machine_real_restart removed - never called */

void nmi_panic_self_stop(struct pt_regs *regs);
/* nmi_shootdown_cpus removed - stub never called */
void run_crash_ipi_callback(struct pt_regs *regs);

#endif  
