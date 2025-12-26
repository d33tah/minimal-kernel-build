 
#ifndef _ASM_X86_MWAIT_H
#define _ASM_X86_MWAIT_H

#include <linux/sched.h>
#include <linux/sched/idle.h>

#include <asm/cpufeature.h>
#include <asm/nospec-branch.h>

/* MWAIT_SUBSTATE_MASK, MWAIT_CSTATE_MASK, MWAIT_SUBSTATE_SIZE, MWAIT_HINT2CSTATE,
   MWAIT_HINT2SUBSTATE, CPUID_MWAIT_LEAF, CPUID5_ECX_*, MWAIT_ECX_*, MWAITX_*,
   TPAUSE_C01_STATE removed - unused */
#define TPAUSE_C02_STATE		0


/* __mwaitx, __sti_mwait, mwait_idle_with_hints removed - unused */

static inline void __tpause(u32 ecx, u32 edx, u32 eax)
{
	 
	asm volatile("tpause %%ecx\n"
		     :
		     : "c"(ecx), "d"(edx), "a"(eax));
}

#endif  
