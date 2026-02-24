/* doublefault_shim removed - asm_exc_double_fault unreachable (TSS IP=0).
 * Keep doublefault_stack for cpu_entry_area GDT setup. */
#include <asm/cpu_entry_area.h>

DEFINE_PER_CPU_PAGE_ALIGNED(struct doublefault_stack, doublefault_stack) = {};
