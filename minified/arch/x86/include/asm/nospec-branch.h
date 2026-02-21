 
#ifndef _ASM_X86_NOSPEC_BRANCH_H_
#define _ASM_X86_NOSPEC_BRANCH_H_

#include <linux/jump_label.h>
#include <linux/objtool.h>
#include <linux/linkage.h>

#include <asm/alternative.h>
#include <asm/cpufeatures.h>
#include <asm/unwind_hints.h>
#include <asm/percpu.h>

#define RSB_CLEAR_LOOPS		32	 

#define __FILL_RETURN_BUFFER(reg, nr, sp)	\
	mov	$(nr/2), reg;			\
771:						\
	ANNOTATE_INTRA_FUNCTION_CALL;		\
	call	772f;				\
773:	 			\
	UNWIND_HINT_EMPTY;			\
	pause;					\
	lfence;					\
	jmp	773b;				\
772:						\
	ANNOTATE_INTRA_FUNCTION_CALL;		\
	call	774f;				\
775:	 			\
	UNWIND_HINT_EMPTY;			\
	pause;					\
	lfence;					\
	jmp	775b;				\
774:						\
	add	$(BITS_PER_LONG/8) * 2, sp;	\
	dec	reg;				\
	jnz	771b;

#ifdef __ASSEMBLY__

.macro ANNOTATE_UNRET_END
.endm

.macro JMP_NOSPEC reg:req
	jmp	*%\reg
.endm

.macro CALL_NOSPEC reg:req
	call	*%\reg
.endm

.macro FILL_RETURN_BUFFER reg:req nr:req ftr:req
	ALTERNATIVE "jmp .Lskip_rsb_\@", "", \ftr
	__FILL_RETURN_BUFFER(\reg,\nr,%_ASM_SP)
.Lskip_rsb_\@:
.endm

/* CONFIG_CPU_UNRET_ENTRY and CONFIG_CPU_IBPB_ENTRY not set - empty macro */
.macro UNTRAIN_RET
.endm

#else  

# define CALL_NOSPEC "call *%[thunk_target]\n"

#endif

#endif  
