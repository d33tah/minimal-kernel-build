 
#ifndef _ASM_GENERIC_ERROR_INJECTION_H
#define _ASM_GENERIC_ERROR_INJECTION_H

#if defined(__KERNEL__) && !defined(__ASSEMBLY__)
enum {
	EI_ETYPE_NONE,		 
	EI_ETYPE_NULL,		 
	EI_ETYPE_ERRNO,		 
	EI_ETYPE_ERRNO_NULL,	 
	EI_ETYPE_TRUE,		 
};

struct error_injection_entry {
	unsigned long	addr;
	int		etype;
};

struct pt_regs;

#define ALLOW_ERROR_INJECTION(fname, _etype)

static inline void override_function_with_return(struct pt_regs *regs) { }
#endif

#endif  
