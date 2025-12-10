#include <linux/perf_event.h>
#include <linux/export.h>
#include <linux/jump_label.h>


/* perf_event_refresh, perf_event_release_kernel, perf_event_read_value,
   perf_event_pause, perf_event_create_kernel_counter, perf_event_sysfs_show,
   register_user_hw_breakpoint, modify_user_hw_breakpoint, unregister_hw_breakpoint
   removed - unused */

void __perf_event_task_sched_in(struct task_struct *prev, struct task_struct *task) { }
void __perf_event_task_sched_out(struct task_struct *prev, struct task_struct *next) { }
void perf_event_task_tick(void) { }

int perf_event_init_task(struct task_struct *child, u64 clone_flags) { return 0; }
void perf_event_fork(struct task_struct *task) { }
void perf_event_free_task(struct task_struct *task) { }
void perf_event_namespaces(struct task_struct *task) { }
void perf_event_delayed_put(struct task_struct *task) { }
void perf_event_exit_task(struct task_struct *task) { }
int perf_event_task_disable(void) { return 0; }
int perf_event_task_enable(void) { return 0; }

void perf_event_mmap(struct vm_area_struct *vma) { }
void perf_event_exec(void) { }
void perf_event_comm(struct task_struct *task, bool exec) { }
void perf_event_text_poke(const void *addr, const void *old_bytes, size_t old_len, 
                          const void *new_bytes, size_t new_len) { }

void __perf_sw_event(u32 event_id, u64 nr, struct pt_regs *regs, u64 addr) { }
void ___perf_sw_event(u32 event_id, u64 nr, struct pt_regs *regs, u64 addr) { }
void perf_bp_event(struct perf_event *event, void *data) { }

void perf_event_init(void) { }

DEFINE_STATIC_KEY_FALSE(perf_sched_events);
struct static_key perf_swevent_enabled[PERF_COUNT_SW_MAX];

void perf_clear_dirty_counters(void) { }
DEFINE_STATIC_KEY_TRUE(rdpmc_never_available_key);
DEFINE_STATIC_KEY_FALSE(rdpmc_always_available_key);

DEFINE_PER_CPU_PAGE_ALIGNED(struct debug_store, cpu_debug_store);
DEFINE_PER_CPU(struct pt_regs, __perf_regs[4]);

/* Perf regs disabled - just define minimum needed */
#define PERF_REG_X86_MAX 32
unsigned int pt_regs_offset[PERF_REG_X86_MAX];

int insn_get_addr_ref(struct insn *insn, struct pt_regs *regs) { return -ENOSYS; }

#include <asm/insn.h>
int insn_decode(struct insn *insn, const void *kaddr, int buf_len, enum insn_mode m) { return -ENOSYS; }

void irq_work_tick(void) { }

