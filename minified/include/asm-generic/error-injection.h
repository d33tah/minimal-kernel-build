/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_ERROR_INJECTION_H
#define _ASM_GENERIC_ERROR_INJECTION_H

#if defined(__KERNEL__) && !defined(__ASSEMBLY__)
enum {
	EI_ETYPE_NONE,		/* Dummy value for undefined case */
	EI_ETYPE_NULL,		/* Return NULL if failure */
	EI_ETYPE_ERRNO,		/* Return -ERRNO if failure */
	EI_ETYPE_ERRNO_NULL,	/* Return -ERRNO or NULL if failure */
	EI_ETYPE_TRUE,		/* Return true if failure */
};

struct error_injection_entry {
	unsigned long	addr;
	int		etype;
};

struct pt_regs;

#define ALLOW_ERROR_INJECTION(fname, _etype)

static inline void override_function_with_return(struct pt_regs *regs) { }
#endif

#endif /* _ASM_GENERIC_ERROR_INJECTION_H */
