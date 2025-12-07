 
#ifndef _ASM_X86_KPROBES_H
#define _ASM_X86_KPROBES_H

/* Inlined from asm-generic/kprobes.h */
#if defined(__KERNEL__) && !defined(__ASSEMBLY__)
# define NOKPROBE_SYMBOL(fname)
# define __kprobes
# define nokprobe_inline	inline
#endif


static inline int kprobe_debug_handler(struct pt_regs *regs) { return 0; }

#endif  
