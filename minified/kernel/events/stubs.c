#include <linux/perf_event.h>
#include <linux/export.h>
#include <linux/jump_label.h>


/* perf_event_refresh, perf_event_release_kernel, perf_event_read_value,
   perf_event_pause, perf_event_create_kernel_counter, perf_event_sysfs_show,
   register_user_hw_breakpoint, modify_user_hw_breakpoint, unregister_hw_breakpoint
   removed - unused */


int perf_event_init_task(struct task_struct *child, u64 clone_flags) { return 0; }
void perf_event_fork(struct task_struct *task) { }
void perf_event_free_task(struct task_struct *task) { }
void perf_event_delayed_put(struct task_struct *task) { }
void perf_event_exit_task(struct task_struct *task) { }

void perf_event_mmap(struct vm_area_struct *vma) { }
void perf_event_exec(void) { }
void perf_event_comm(struct task_struct *task, bool exec) { }
/* __perf_sw_event inlined as empty in perf_event.h */
void perf_event_init(void) { }

void perf_clear_dirty_counters(void) { }
DEFINE_STATIC_KEY_TRUE(rdpmc_never_available_key);
DEFINE_STATIC_KEY_FALSE(rdpmc_always_available_key);

DEFINE_PER_CPU_PAGE_ALIGNED(struct debug_store, cpu_debug_store);

/* Perf regs - just define minimum needed for pt_regs_offset array */
#define PERF_REG_X86_MAX 32
unsigned int pt_regs_offset[PERF_REG_X86_MAX];

void irq_work_tick(void) { }

