
#include <linux/personality.h>
#include <linux/mm.h>
#include <linux/limits.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <asm/elf.h>
#include <asm/io.h>

/* phys_addr_valid, task_size_32bit removed - never called */

unsigned long task_size_64bit(int full_addr_space)
{
	return full_addr_space ? TASK_SIZE_MAX : DEFAULT_MAP_WINDOW;
}

/* stack_maxrandom_size, mmap32_rnd_bits, mmap64_rnd_bits removed - no ASLR */

#define SIZE_128M (128 * 1024 * 1024UL)

/* mmap_is_legacy simplified - sysctl_legacy_va_layout always 0 */
static int mmap_is_legacy(void)
{
	return (current->personality & ADDR_COMPAT_LAYOUT) ? 1 : 0;
}

/* arch_rnd, arch_mmap_rnd removed - no ASLR randomization, PF_RANDOMIZE never set */

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

/* mmap_legacy_base inlined - random_factor always 0 */
/* arch_pick_mmap_base inlined into arch_pick_mmap_layout */

void arch_pick_mmap_layout(struct mm_struct *mm, struct rlimit *rlim_stack)
{
	unsigned long task_size = task_size_64bit(0);

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

/* get_mmap_base, mmap_address_hint_valid, pfn_modify_allowed removed - never called or always true */
