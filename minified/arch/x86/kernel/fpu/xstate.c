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

#include <asm/tlbflush.h>
#include <asm/prctl.h>
#include <asm/elf.h>

#include "context.h"
#include "internal.h"
#include "legacy.h"
#include "xstate.h"

// Stub: Initialize CPU xstate
void fpu__init_cpu_xstate(void) { }

// Stub: Initialize system xstate
void __init fpu__init_system_xstate(unsigned int legacy_size) { }

// Stub: Resume CPU FPU state
void fpu__resume_cpu(void) { }

// Stub: xstate prctl
long fpu_xstate_prctl(int option, unsigned long arg2)
{
	return 0;
}
