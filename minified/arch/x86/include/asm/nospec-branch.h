/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_X86_NOSPEC_BRANCH_H_
#define _ASM_X86_NOSPEC_BRANCH_H_

#include <linux/linkage.h>

#define RETPOLINE_THUNK_SIZE 32

#ifdef __ASSEMBLY__

/* clang-format off */
.macro CALL_NOSPEC reg:req
	call	*%\reg
.endm
/* clang-format on */

#else /* __ASSEMBLY__ */

typedef u8 retpoline_thunk_t[RETPOLINE_THUNK_SIZE];

#define CALL_NOSPEC "call *%[thunk_target]\n"

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_NOSPEC_BRANCH_H_ */
