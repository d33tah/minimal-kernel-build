// Stubbed xstate.c for minimal kernel
// Original: 1,026 LOC
// Stubbed: ~60 LOC
// Date: 2025-11-21 12:55

#include <linux/bitops.h>
#include <linux/compat.h>
#include <linux/cpu.h>
#include <linux/mman.h>
#include <linux/nospec.h>
/* seq_file.h removed - header is empty */
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>

#include <asm/fpu/api.h>
/* asm/fpu/regset.h removed - not used */
#include <asm/fpu/signal.h>

#include <asm/tlbflush.h>
#include <asm/prctl.h>
#include <asm/elf.h>

#include "context.h"
#include "internal.h"
#include "legacy.h"
#include "xstate.h"

/* cpu_has_xfeatures removed - never called */

/* fpu__init_cpu_xstate, fpu__init_system_xstate removed - empty stubs */
/* fpu__resume_cpu, copy_sigframe_from_user_to_xstate, fpu_xstate_prctl removed */
