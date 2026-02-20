
#include <linux/mm.h>
#include <linux/sched/mm.h>

void arch_pick_mmap_layout(struct mm_struct *mm, struct rlimit *rlim_stack)
{
	mm->get_unmapped_area = arch_get_unmapped_area;
	mm->mmap_legacy_base = __TASK_UNMAPPED_BASE(DEFAULT_MAP_WINDOW);
	mm->mmap_base = mm->mmap_legacy_base;
}
