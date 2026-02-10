 
#ifndef _ASM_X86_PROTO_H
#define _ASM_X86_PROTO_H

#ifndef LDT_ENTRY_SIZE
#define LDT_ENTRY_SIZE 8 /* inlined from asm/ldt.h */
#endif

void entry_INT80_32(void);
void entry_SYSENTER_32(void);
void __begin_SYSENTER_singlestep_region(void);
void __end_SYSENTER_singlestep_region(void);

void x86_configure_nx(void);

/* do_arch_prctl_common removed - arch_prctl uses COND_SYSCALL */

#endif  
