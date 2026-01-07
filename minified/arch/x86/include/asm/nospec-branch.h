 

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

 
/* CONFIG_CPU_UNRET_ENTRY and CONFIG_CPU_IBPB_ENTRY not set - empty macro */
.macro UNTRAIN_RET
.endm

#else  

typedef u8 retpoline_thunk_t[RETPOLINE_THUNK_SIZE];
extern retpoline_thunk_t __x86_indirect_thunk_array[];

extern void entry_ibpb(void);

# define CALL_NOSPEC "call *%[thunk_target]\n"
# define THUNK_TARGET(addr) [thunk_target] "rm" (addr)

/* Removed unused spectre_v2_mitigation, spectre_v2_user_mitigation,
   ssb_mitigation enums, __indirect_thunk_start/end */

static __always_inline
void alternative_msr_write(unsigned int msr, u64 val, unsigned int feature)
{
	asm volatile(ALTERNATIVE("", "wrmsr", %c[feature])
		: : "c" (msr),
		    "a" ((u32)val),
		    "d" ((u32)(val >> 32)),
		    [feature] "i" (feature)
		: "memory");
}

static inline void indirect_branch_prediction_barrier(void)
{
	u64 val = PRED_CMD_IBPB;

	alternative_msr_write(MSR_IA32_PRED_CMD, val, X86_FEATURE_USE_IBPB);
}

 
extern u64 x86_spec_ctrl_base;
DECLARE_PER_CPU(u64, x86_spec_ctrl_current);
extern void write_spec_ctrl_current(u64 val, bool force);
extern u64 spec_ctrl_current(void);

DECLARE_STATIC_KEY_FALSE(switch_to_cond_stibp);
DECLARE_STATIC_KEY_FALSE(switch_mm_cond_ibpb);
DECLARE_STATIC_KEY_FALSE(switch_mm_always_ibpb);

DECLARE_STATIC_KEY_FALSE(mds_user_clear);
DECLARE_STATIC_KEY_FALSE(mds_idle_clear);

DECLARE_STATIC_KEY_FALSE(switch_mm_cond_l1d_flush);

#include <asm/segment.h>

 
static __always_inline void mds_clear_cpu_buffers(void)
{
	static const u16 ds = __KERNEL_DS;

	 
	asm volatile("verw %[ds]" : : [ds] "m" (ds) : "cc");
}

 
static __always_inline void mds_user_clear_cpu_buffers(void)
{
	if (static_branch_likely(&mds_user_clear))
		mds_clear_cpu_buffers();
}

 
static inline void mds_idle_clear_cpu_buffers(void)
{
	if (static_branch_likely(&mds_idle_clear))
		mds_clear_cpu_buffers();
}

#endif  

#endif  
