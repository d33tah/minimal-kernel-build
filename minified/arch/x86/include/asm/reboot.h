 
#ifndef _ASM_X86_REBOOT_H
#define _ASM_X86_REBOOT_H

#include <linux/kdebug.h>

struct pt_regs;

void native_machine_shutdown(void);

/* nmi_shootdown_cpus removed - stub never called */
void run_crash_ipi_callback(struct pt_regs *regs);

#endif  
