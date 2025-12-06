#ifndef _LINUX_ERROR_INJECTION_H
#define _LINUX_ERROR_INJECTION_H

#include <linux/compiler.h>
#include <asm-generic/error-injection.h>


static inline bool within_error_injection_list(unsigned long addr)
{
	return false;
}

static inline int get_injectable_error_type(unsigned long addr)
{
	return EI_ETYPE_NONE;
}


#endif  
