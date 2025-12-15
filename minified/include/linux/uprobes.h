/* Minimal uprobes.h - stubs only */
#ifndef _LINUX_UPROBES_H
#define _LINUX_UPROBES_H

struct vm_area_struct;
struct mm_struct;
struct task_struct;
struct pt_regs;

struct uprobes_state {
};

static inline void uprobes_init(void) {}

#define uprobe_get_trap_addr(regs)	instruction_pointer(regs)

/* uprobe_mmap removed - unused */
static inline void uprobe_munmap(struct vm_area_struct *vma, unsigned long start, unsigned long end) {}
/* uprobe_start_dup_mmap, uprobe_end_dup_mmap, uprobe_dup_mmap removed - unused */
static inline void uprobe_notify_resume(struct pt_regs *regs) {}
/* uprobe_deny_signal removed - unused */
static inline void uprobe_free_utask(struct task_struct *t) {}
static inline void uprobe_copy_process(struct task_struct *t, unsigned long flags) {}
static inline void uprobe_clear_state(struct mm_struct *mm) {}

#endif /* _LINUX_UPROBES_H */
