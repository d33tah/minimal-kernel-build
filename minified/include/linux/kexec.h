#ifndef LINUX_KEXEC_H
#define LINUX_KEXEC_H

#if !defined(__ASSEMBLY__)

#include <asm/io.h>



struct pt_regs;
struct task_struct;
static inline int kexec_should_crash(struct task_struct *p) { return 0; }

#endif  

#endif  
