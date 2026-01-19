#ifndef _ASM_X86_FRAME_H
#define _ASM_X86_FRAME_H

#include <asm/asm.h>

#ifdef __ASSEMBLY__

.macro ENCODE_FRAME_POINTER ptregs_offset=0
.endm

#else

#define ENCODE_FRAME_POINTER
/* encode_frame_pointer function removed - always returned 0 */

#endif

#define FRAME_BEGIN
#define FRAME_END
#define FRAME_OFFSET 0

#endif
