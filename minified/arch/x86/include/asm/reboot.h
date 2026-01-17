 
#ifndef _ASM_X86_REBOOT_H
#define _ASM_X86_REBOOT_H

#include <linux/kdebug.h>

struct pt_regs;

/* struct machine_ops removed - unused */

/* native_machine_crash_shutdown removed - declared but never implemented */
/* native_machine_shutdown removed - never called */
/* machine_real_restart removed - never called */

/* nmi_panic_self_stop removed - only called by nmi_panic which was removed */
/* nmi_shootdown_cpus, run_crash_ipi_callback removed - stub never called */

#endif  
