/* Minimal uprobes.h - stubs only */
#ifndef _LINUX_UPROBES_H
#define _LINUX_UPROBES_H

struct task_struct;

#define uprobe_get_trap_addr(regs)	instruction_pointer(regs)

#endif /* _LINUX_UPROBES_H */
