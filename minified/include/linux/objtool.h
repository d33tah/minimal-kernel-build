 
#ifndef _LINUX_OBJTOOL_H
#define _LINUX_OBJTOOL_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

 
struct unwind_hint {
	u32		ip;
	s16		sp_offset;
	u8		sp_reg;
	u8		type;
	u8		end;
};
#endif

 
#define UNWIND_HINT_TYPE_CALL		0
#define UNWIND_HINT_TYPE_REGS		1
#define UNWIND_HINT_TYPE_REGS_PARTIAL	2
#define UNWIND_HINT_TYPE_FUNC		3
#define UNWIND_HINT_TYPE_ENTRY		4
#define UNWIND_HINT_TYPE_SAVE		5
#define UNWIND_HINT_TYPE_RESTORE	6


#ifndef __ASSEMBLY__

#define UNWIND_HINT(sp_reg, sp_offset, type, end)	\
	"\n\t"
#define STACK_FRAME_NON_STANDARD(func)
#define STACK_FRAME_NON_STANDARD_FP(func)
#define ANNOTATE_NOENDBR
#define ASM_REACHABLE
#else
#define ANNOTATE_INTRA_FUNCTION_CALL
.macro UNWIND_HINT type:req sp_reg=0 sp_offset=0 end=0
.endm
.macro STACK_FRAME_NON_STANDARD func:req
.endm
.macro ANNOTATE_NOENDBR
.endm
.macro REACHABLE
.endm
#endif


#endif  
