#ifndef LINUX_KEXEC_H
#define LINUX_KEXEC_H

#if !defined(__ASSEMBLY__)

#include <linux/crash_core.h>
#include <asm/io.h>



struct pt_regs;
struct task_struct;
static inline void __crash_kexec(struct pt_regs *regs) { }
static inline void crash_kexec(struct pt_regs *regs) { }
static inline int kexec_should_crash(struct task_struct *p) { return 0; }

#endif  

#endif  
