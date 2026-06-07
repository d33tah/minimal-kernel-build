
#ifndef _ASM_X86_UCONTEXT_H
#define _ASM_X86_UCONTEXT_H

/* 32-bit only kernel - removed x86_64 defines */
#define UC_FP_XSTATE	0x1

/* Inlined from asm-generic/ucontext.h */
struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	struct sigcontext uc_mcontext;
	sigset_t	  uc_sigmask;
};

#endif  
