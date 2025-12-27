 
#ifndef _ASM_X86_REBOOT_H
#define _ASM_X86_REBOOT_H

#include <linux/kdebug.h>

struct pt_regs;

struct machine_ops {
	void (*restart)(char *cmd);
	void (*halt)(void);
	void (*power_off)(void);
	void (*shutdown)(void);
	void (*crash_shutdown)(struct pt_regs *);
	void (*emergency_restart)(void);
};


/* native_machine_crash_shutdown removed - declared but never implemented */
void native_machine_shutdown(void);
void __noreturn machine_real_restart(unsigned int type);
 
#define MRR_BIOS	0
#define MRR_APM		1

void nmi_panic_self_stop(struct pt_regs *regs);
/* nmi_shootdown_cpus removed - stub never called */
void run_crash_ipi_callback(struct pt_regs *regs);

#endif  
