/* Minimal stubs for perf events subsystem */
#include <linux/perf_event.h>
#include <linux/export.h>
#include <linux/static_key.h>

/* Match exact types from include/linux/perf_event.h */

/* Basic perf event functions */
int perf_event_refresh(struct perf_event *event, int refresh) { return 0; }

int perf_event_release_kernel(struct perf_event *event) { return 0; }

u64 perf_event_read_value(struct perf_event *event, u64 *enabled, u64 *running) { return 0; }

u64 perf_event_pause(struct perf_event *event, bool reset) { return 0; }

struct perf_event *perf_event_create_kernel_counter(struct perf_event_attr *attr, int cpu,
                                                     struct task_struct *task,
                                                     perf_overflow_handler_t callback,
                                                     void *context) 
{ 
    return ERR_PTR(-ENOSYS); 
}

ssize_t perf_event_sysfs_show(struct device *dev, struct device_attribute *attr, char *page) 
{ 
    return 0; 
}

/* HW breakpoints */
struct perf_event *register_user_hw_breakpoint(struct perf_event_attr *attr,
                                                perf_overflow_handler_t triggered,
                                                void *context, struct task_struct *tsk) 
{ 
    return ERR_PTR(-ENOSYS); 
}

int modify_user_hw_breakpoint(struct perf_event *bp, struct perf_event_attr *attr) 
{ 
    return -ENOSYS; 
}

void unregister_hw_breakpoint(struct perf_event *bp) { }

/* Task/scheduler integration */
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

/* Memory/exec events */
void perf_event_mmap(struct vm_area_struct *vma) { }
void perf_event_exec(void) { }
void perf_event_comm(struct task_struct *task, bool exec) { }
void perf_event_text_poke(const void *addr, const void *old_bytes, size_t old_len, 
                          const void *new_bytes, size_t new_len) { }

/* Software events */
void __perf_sw_event(u32 event_id, u64 nr, struct pt_regs *regs, u64 addr) { }
void ___perf_sw_event(u32 event_id, u64 nr, struct pt_regs *regs, u64 addr) { }
void perf_bp_event(struct perf_event *event, void *data) { }

/* Init */
void perf_event_init(void) { }

/* Global data - use correct types from headers */
DEFINE_STATIC_KEY_FALSE(perf_sched_events);
struct static_key perf_swevent_enabled[PERF_COUNT_SW_MAX];

/* x86 specific */
void perf_clear_dirty_counters(void) { }
DEFINE_STATIC_KEY_TRUE(rdpmc_never_available_key);
DEFINE_STATIC_KEY_FALSE(rdpmc_always_available_key);

/* Per-CPU data */
DEFINE_PER_CPU_PAGE_ALIGNED(struct debug_store, cpu_debug_store);
DEFINE_PER_CPU(struct pt_regs, __perf_regs[4]);

/* x86 perf_regs stubs - pt_regs_offset array */
#include <asm/perf_regs.h>
#ifndef PERF_REG_X86_MAX
#define PERF_REG_X86_MAX 32
#endif
unsigned int pt_regs_offset[PERF_REG_X86_MAX];

/* x86 insn_get_addr_ref stub */
int insn_get_addr_ref(struct insn *insn, struct pt_regs *regs) { return -ENOSYS; }

/* x86 insn_decode stub */
#include <asm/insn.h>
int insn_decode(struct insn *insn, const void *kaddr, int buf_len, enum insn_mode m) { return -ENOSYS; }

/* irq_work_tick stub */
void irq_work_tick(void) { }

