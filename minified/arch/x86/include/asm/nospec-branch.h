 

#ifndef _ASM_X86_NOSPEC_BRANCH_H_
#define _ASM_X86_NOSPEC_BRANCH_H_

#include <linux/linkage.h>


#define RETPOLINE_THUNK_SIZE	32

 

#ifdef __ASSEMBLY__

 

 

 
.macro CALL_NOSPEC reg:req
	call	*%\reg
.endm

  

 
#else

typedef u8 retpoline_thunk_t[RETPOLINE_THUNK_SIZE];

# define CALL_NOSPEC "call *%[thunk_target]\n"

 

#endif

#endif  
