
#ifndef _ASM_X86_IBT_H
#define _ASM_X86_IBT_H

#include <linux/types.h>


#define HAS_KERNEL_IBT	0

#ifndef __ASSEMBLY__

/* ASM_ENDBR, __noendbr removed - unused */

#else

#define ENDBR

#endif


#define ENDBR_INSN_SIZE		(4*HAS_KERNEL_IBT)

#endif
