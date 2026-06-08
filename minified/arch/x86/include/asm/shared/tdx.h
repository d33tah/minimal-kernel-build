 
#ifndef _ASM_X86_SHARED_TDX_H
#define _ASM_X86_SHARED_TDX_H

#include <linux/bits.h>
#include <linux/types.h>

#define TDX_HYPERCALL_STANDARD  0

#define TDX_HCALL_HAS_OUTPUT	BIT(0)
#define TDX_HCALL_ISSUE_STI	BIT(1)

#define TDX_CPUID_LEAF_ID	0x21
#define TDX_IDENT		"IntelTDX    "

#ifndef __ASSEMBLY__

#endif
#endif  
