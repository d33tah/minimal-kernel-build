
#include <asm/asm-offsets.h>
#include <asm/processor-flags.h>
#include <asm/nospec-branch.h>

#undef UNWIND_HINT_IRET_REGS
#define UNWIND_HINT_IRET_REGS

/* clang-format off */
.macro STACKLEAK_ERASE
.endm
