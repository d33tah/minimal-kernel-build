#include <linux/perf_event.h>
#include <linux/export.h>
#include <linux/jump_label.h>


/* perf_event_refresh, perf_event_release_kernel, perf_event_read_value,
   perf_event_pause, perf_event_create_kernel_counter, perf_event_sysfs_show,
   register_user_hw_breakpoint, modify_user_hw_breakpoint, unregister_hw_breakpoint
   removed - unused */


void perf_clear_dirty_counters(void) { }
DEFINE_STATIC_KEY_TRUE(rdpmc_never_available_key);
DEFINE_STATIC_KEY_FALSE(rdpmc_always_available_key);

DEFINE_PER_CPU_PAGE_ALIGNED(struct debug_store, cpu_debug_store);

/* Perf regs - just define minimum needed for pt_regs_offset array */
#define PERF_REG_X86_MAX 32
unsigned int pt_regs_offset[PERF_REG_X86_MAX];

void irq_work_tick(void) { }

