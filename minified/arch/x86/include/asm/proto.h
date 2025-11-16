 
#ifndef _ASM_X86_PROTO_H
#define _ASM_X86_PROTO_H

#include <asm/ldt.h>

struct task_struct;

 

void syscall_init(void);


void entry_INT80_32(void);
void entry_SYSENTER_32(void);
void __begin_SYSENTER_singlestep_region(void);
void __end_SYSENTER_singlestep_region(void);


void x86_configure_nx(void);

extern int reboot_force;

long do_arch_prctl_common(int option, unsigned long arg2);

#endif  
