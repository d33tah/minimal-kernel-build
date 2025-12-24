#include <linux/perf_event.h>
#include <linux/export.h>
#include <linux/jump_label.h>
int perf_event_init_task(struct task_struct *child, u64 clone_flags)
{
	return 0;
}
void perf_event_fork(struct task_struct *task)
{
}
void perf_event_free_task(struct task_struct *task)
{
}
void perf_event_delayed_put(struct task_struct *task)
{
}
void perf_event_exit_task(struct task_struct *task)
{
}
void perf_event_mmap(struct vm_area_struct *vma)
{
}
void perf_event_exec(void)
{
}
void perf_event_comm(struct task_struct *task, bool exec)
{
}
void perf_event_init(void)
{
}
void perf_clear_dirty_counters(void)
{
}
DEFINE_STATIC_KEY_TRUE(rdpmc_never_available_key);
DEFINE_STATIC_KEY_FALSE(rdpmc_always_available_key);
DEFINE_PER_CPU_PAGE_ALIGNED(struct debug_store, cpu_debug_store);
#define PERF_REG_X86_MAX 32
unsigned int pt_regs_offset[PERF_REG_X86_MAX];
void irq_work_tick(void)
{
}
