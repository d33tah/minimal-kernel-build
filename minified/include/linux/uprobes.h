 
#ifndef _LINUX_UPROBES_H
#define _LINUX_UPROBES_H
 

#include <linux/errno.h>
#include <linux/rbtree.h>
#include <linux/types.h>
#include <linux/wait.h>

struct vm_area_struct;
struct mm_struct;
struct inode;
struct notifier_block;
struct page;

#define UPROBE_HANDLER_REMOVE		1
#define UPROBE_HANDLER_MASK		1

#define MAX_URETPROBE_DEPTH		64

enum uprobe_filter_ctx {
	UPROBE_FILTER_REGISTER,
	UPROBE_FILTER_UNREGISTER,
	UPROBE_FILTER_MMAP,
};

struct uprobe_consumer {
	int (*handler)(struct uprobe_consumer *self, struct pt_regs *regs);
	int (*ret_handler)(struct uprobe_consumer *self,
				unsigned long func,
				struct pt_regs *regs);
	bool (*filter)(struct uprobe_consumer *self,
				enum uprobe_filter_ctx ctx,
				struct mm_struct *mm);

	struct uprobe_consumer *next;
};

struct uprobes_state {
};

static inline void uprobes_init(void)
{
}

#define uprobe_get_trap_addr(regs)	instruction_pointer(regs)

static inline int
uprobe_register(struct inode *inode, loff_t offset, struct uprobe_consumer *uc)
{
	return -ENOSYS;
}
static inline int uprobe_register_refctr(struct inode *inode, loff_t offset, loff_t ref_ctr_offset, struct uprobe_consumer *uc)
{
	return -ENOSYS;
}
static inline int
uprobe_apply(struct inode *inode, loff_t offset, struct uprobe_consumer *uc, bool add)
{
	return -ENOSYS;
}
static inline void
uprobe_unregister(struct inode *inode, loff_t offset, struct uprobe_consumer *uc)
{
}
static inline int uprobe_mmap(struct vm_area_struct *vma)
{
	return 0;
}
static inline void
uprobe_munmap(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
}
static inline void uprobe_start_dup_mmap(void)
{
}
static inline void uprobe_end_dup_mmap(void)
{
}
static inline void
uprobe_dup_mmap(struct mm_struct *oldmm, struct mm_struct *newmm)
{
}
static inline void uprobe_notify_resume(struct pt_regs *regs)
{
}
static inline bool uprobe_deny_signal(void)
{
	return false;
}
static inline void uprobe_free_utask(struct task_struct *t)
{
}
static inline void uprobe_copy_process(struct task_struct *t, unsigned long flags)
{
}
static inline void uprobe_clear_state(struct mm_struct *mm)
{
}
#endif	 
