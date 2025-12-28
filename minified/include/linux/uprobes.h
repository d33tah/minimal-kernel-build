/* Minimal uprobes.h - stubs only */
#ifndef _LINUX_UPROBES_H
#define _LINUX_UPROBES_H

struct vm_area_struct;
struct mm_struct;
struct task_struct;
struct pt_regs;

struct uprobes_state {
};

#define uprobe_get_trap_addr(regs)	instruction_pointer(regs)

#endif /* _LINUX_UPROBES_H */
