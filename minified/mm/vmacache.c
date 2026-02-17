#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/mm.h>
#include <linux/vmacache.h>

#define VMACACHE_SHIFT PMD_SHIFT
#define VMACACHE_HASH(addr) ((addr >> VMACACHE_SHIFT) & VMACACHE_MASK)

static inline bool vmacache_valid_mm(struct mm_struct *mm)
{
	return current->mm == mm && !(current->flags & PF_KTHREAD);
}

void vmacache_update(unsigned long addr, struct vm_area_struct *newvma)
{
	if (vmacache_valid_mm(newvma->vm_mm))
		current->vmacache.vmas[VMACACHE_HASH(addr)] = newvma;
}

struct vm_area_struct *vmacache_find(struct mm_struct *mm, unsigned long addr)
{
	int idx = VMACACHE_HASH(addr);
	int i;
	struct task_struct *curr = current;
	if (!vmacache_valid_mm(mm))
		return NULL;
	if (mm->vmacache_seqnum != curr->vmacache.seqnum) {
		curr->vmacache.seqnum = mm->vmacache_seqnum;
		vmacache_flush(curr);
		return NULL;
	}

	for (i = 0; i < VMACACHE_SIZE; i++) {
		struct vm_area_struct *vma = current->vmacache.vmas[idx];

		if (vma) {
			if (vma->vm_start <= addr && vma->vm_end > addr) {
				return vma;
			}
		}
		if (++idx == VMACACHE_SIZE)
			idx = 0;
	}

	return NULL;
}
