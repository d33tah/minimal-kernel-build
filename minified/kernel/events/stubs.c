/* Minimal perf event stubs - only definitions needed by other code */
#include <linux/perf_event.h>
#include <linux/jump_label.h>
#include <linux/errno.h>

DEFINE_STATIC_KEY_TRUE(rdpmc_never_available_key);
DEFINE_STATIC_KEY_FALSE(rdpmc_always_available_key);
/* CONFIG_PERF_EVENTS cpu_debug_store removed - not set */

/* pt_regs_offset stub - insn-eval.c was removed */
int pt_regs_offset(struct pt_regs *regs, int regno)
{
	return -1;
}
