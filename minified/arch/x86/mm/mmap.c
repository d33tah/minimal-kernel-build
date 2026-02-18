
#include <linux/mm.h>
#include <linux/sched/mm.h>

#define SIZE_128M (128 * 1024 * 1024UL)

static int mmap_is_legacy(void)
{
	return (current->personality & ADDR_COMPAT_LAYOUT) ? 1 : 0;
}

static unsigned long mmap_base(unsigned long task_size,
			       struct rlimit *rlim_stack)
{
	unsigned long gap = rlim_stack->rlim_cur + stack_guard_gap;
	unsigned long gap_min, gap_max;

	gap_min = SIZE_128M;
	gap_max = (task_size / 6) * 5;

	if (gap < gap_min)
		gap = gap_min;
	else if (gap > gap_max)
		gap = gap_max;

	return PAGE_ALIGN(task_size - gap);
}

void arch_pick_mmap_layout(struct mm_struct *mm, struct rlimit *rlim_stack)
{
	unsigned long task_size = DEFAULT_MAP_WINDOW;

	if (mmap_is_legacy())
		mm->get_unmapped_area = arch_get_unmapped_area;
	else
		mm->get_unmapped_area = arch_get_unmapped_area_topdown;

	mm->mmap_legacy_base = __TASK_UNMAPPED_BASE(task_size);
	if (mmap_is_legacy())
		mm->mmap_base = mm->mmap_legacy_base;
	else
		mm->mmap_base = mmap_base(task_size, rlim_stack);
}
