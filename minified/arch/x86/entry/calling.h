 
#include <linux/jump_label.h>
#include <asm/unwind_hints.h>
#include <asm/cpufeatures.h>
#include <asm/page_types.h>
#include <asm/percpu.h>
#include <asm/asm-offsets.h>
#include <asm/processor-flags.h>
#include <asm/ptrace-abi.h>
#include <asm/msr.h>
#include <asm/nospec-branch.h>

 

# undef		UNWIND_HINT_IRET_REGS
# define	UNWIND_HINT_IRET_REGS

.macro STACKLEAK_ERASE
.endm


.macro GET_PERCPU_BASE reg:req
	movq	pcpu_unit_offsets(%rip), \reg
.endm

