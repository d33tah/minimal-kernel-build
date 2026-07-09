/* Minimal uprobes.h - stubs only */
#ifndef _LINUX_UPROBES_H
#define _LINUX_UPROBES_H

struct mm_struct;
struct task_struct;
struct pt_regs;

#define uprobe_get_trap_addr(regs)	instruction_pointer(regs)

#endif /* _LINUX_UPROBES_H */
