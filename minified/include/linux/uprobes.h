/* Minimal uprobes.h - stubs only */
#ifndef _LINUX_UPROBES_H
#define _LINUX_UPROBES_H
#define uprobe_get_trap_addr(regs)	instruction_pointer(regs)
/* struct uprobes_state removed - empty and never used */
#endif
