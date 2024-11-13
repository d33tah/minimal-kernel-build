/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_FRAME_H
#define _ASM_X86_FRAME_H

#include <asm/asm.h>

/*
 * These are stack frame creation macros.  They should be used by every
 * callable non-leaf asm function to make kernel stack traces more reliable.
 */


#ifdef __ASSEMBLY__

.macro ENCODE_FRAME_POINTER ptregs_offset=0
.endm

#else /* !__ASSEMBLY */

#define ENCODE_FRAME_POINTER

static inline unsigned long encode_frame_pointer(struct pt_regs *regs)
{
	return 0;
}

#endif

#define FRAME_BEGIN
#define FRAME_END
#define FRAME_OFFSET 0


#endif /* _ASM_X86_FRAME_H */
