// Stubbed xstate.c for minimal kernel
// Original: 1,026 LOC
// Stubbed: ~60 LOC
// Date: 2025-11-21 12:55

#include <linux/bitops.h>
#include <linux/compat.h>
#include <linux/cpu.h>
#include <linux/mman.h>
#include <linux/nospec.h>
#include <linux/pkeys.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>

#include <asm/fpu/api.h>
#include <asm/fpu/regset.h>
#include <asm/fpu/signal.h>
#include <asm/fpu/xcr.h>

#include <asm/tlbflush.h>
#include <asm/prctl.h>
#include <asm/elf.h>

#include "context.h"
#include "internal.h"
#include "legacy.h"
#include "xstate.h"

// Stub: Check if CPU has xfeatures
int cpu_has_xfeatures(u64 xfeatures_needed, const char **feature_name)
{
	return 1; // Always claim features are present
}

// Stub: Initialize CPU xstate
void fpu__init_cpu_xstate(void) { }

// Stub: Get xfeature size
int xfeature_size(int xfeature_nr)
{
	return 0;
}

// Stub: Initialize system xstate
void __init fpu__init_system_xstate(unsigned int legacy_size) { }

// Stub: Resume CPU FPU state
void fpu__resume_cpu(void) { }

// Stub: Copy xstate to user buffer
void __copy_xstate_to_uabi_buf(struct membuf to, struct fpstate *fpstate,
			       u32 pkru_val, enum xstate_copy_mode copy_mode)
{ }

void copy_xstate_to_uabi_buf(struct membuf to, struct task_struct *tsk,
			     enum xstate_copy_mode copy_mode)
{ }

// Stub: Copy from user to xstate
int copy_uabi_from_kernel_to_xstate(struct fpstate *fpstate, const void *kbuf)
{
	return 0;
}

int copy_sigframe_from_user_to_xstate(struct fpstate *fpstate,
				      const void __user *ubuf)
{
	return 0;
}

// Stub: Save/restore extended state
void xsaves(struct xregs_state *xstate, u64 mask) { }
void xrstors(struct xregs_state *xstate, u64 mask) { }

// Stub: Clear xstate component
void fpstate_clear_xstate_component(struct fpstate *fps, unsigned int xfeature) { }

// Stub: Guest permission
u64 xstate_get_guest_group_perm(void)
{
	return 0;
}

// Stub: xstate prctl
long fpu_xstate_prctl(int option, unsigned long arg2)
{
	return 0;
}
