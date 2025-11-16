 
#ifndef _ASM_X86_KPROBES_H
#define _ASM_X86_KPROBES_H
 

#include <asm-generic/kprobes.h>


static inline int kprobe_debug_handler(struct pt_regs *regs) { return 0; }

#endif  
