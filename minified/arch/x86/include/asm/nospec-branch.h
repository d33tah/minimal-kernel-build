 

#ifndef _ASM_X86_NOSPEC_BRANCH_H_
#define _ASM_X86_NOSPEC_BRANCH_H_

#include <linux/jump_label.h>
#include <linux/objtool.h>
#include <linux/linkage.h>

#include <asm/alternative.h>
#include <asm/cpufeatures.h>
#include <asm/msr-index.h>
#include <asm/unwind_hints.h>
#include <asm/percpu.h>

#define RETPOLINE_THUNK_SIZE	32

 

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

 
.macro ANNOTATE_RETPOLINE_SAFE
	.Lannotate_\@:
	.pushsection .discard.retpoline_safe
	_ASM_PTR .Lannotate_\@
	.popsection
.endm

 
#define ANNOTATE_UNRET_SAFE ANNOTATE_RETPOLINE_SAFE

 
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

#define CALL_ZEN_UNTRAIN_RET	""

 
.macro UNTRAIN_RET
#if defined(CONFIG_CPU_UNRET_ENTRY) || defined(CONFIG_CPU_IBPB_ENTRY)
	ANNOTATE_UNRET_END
	ALTERNATIVE_2 "",						\
	              CALL_ZEN_UNTRAIN_RET, X86_FEATURE_UNRET,		\
		      "call entry_ibpb", X86_FEATURE_ENTRY_IBPB
#endif
.endm

#else  

#define ANNOTATE_RETPOLINE_SAFE					\
	"999:\n\t"						\
	".pushsection .discard.retpoline_safe\n\t"		\
	_ASM_PTR " 999b\n\t"					\
	".popsection\n\t"

typedef u8 retpoline_thunk_t[RETPOLINE_THUNK_SIZE];
extern retpoline_thunk_t __x86_indirect_thunk_array[];

extern void __x86_return_thunk(void);
extern void entry_ibpb(void);

# define CALL_NOSPEC "call *%[thunk_target]\n"
# define THUNK_TARGET(addr) [thunk_target] "rm" (addr)

 
extern char __indirect_thunk_start[];
extern char __indirect_thunk_end[];


extern u64 x86_spec_ctrl_base;
extern void write_spec_ctrl_current(u64 val, bool force);

#endif

#endif  
