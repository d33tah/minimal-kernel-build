/* Minimal ptrace stubs */
#include <linux/ptrace.h>
/* __ptrace_link removed - never called */
void __ptrace_unlink(struct task_struct *child)
{
}
void exit_ptrace(struct task_struct *tracer, struct list_head *dead)
{
}
