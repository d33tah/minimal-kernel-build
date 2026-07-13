 
#ifndef __X86_KERNEL_FPU_XSTATE_H
#define __X86_KERNEL_FPU_XSTATE_H

#include <asm/cpufeature.h>
#include <asm/fpu/xstate.h>

/*
 * os_xsave()/os_xrstor() and the XSAVE/XRSTOR instruction wrappers were
 * removed: XSAVE is absent on this build's boot CPU (use_xsave() is always
 * false), so the only FPU save/restore paths that run are the legacy
 * fxsave/fxrstor/fsave ones in core.c.
 */

#endif
