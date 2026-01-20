
#include <linux/personality.h>
#include <linux/mm.h>
#include <linux/limits.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/compat.h>
#include <asm/elf.h>
#include <asm/io.h>

/* phys_addr_valid removed - never called in this file */

unsigned long task_size_32bit(void)
{
	return IA32_PAGE_OFFSET;
}

unsigned long task_size_64bit(int full_addr_space)
{
	return full_addr_space ? TASK_SIZE_MAX : DEFAULT_MAP_WINDOW;
}

/* stack_maxrandom_size always returns 0 - PF_RANDOMIZE never set */
static unsigned long stack_maxrandom_size(unsigned long task_size)
{
	return 0;
}

/* mmap32_rnd_bits, mmap64_rnd_bits removed - no ASLR */

#define SIZE_128M (128 * 1024 * 1024UL)

static int mmap_is_legacy(void)
{
	if (current->personality & ADDR_COMPAT_LAYOUT)
		return 1;

	return sysctl_legacy_va_layout;
}

/* arch_rnd, arch_mmap_rnd removed - no ASLR randomization, PF_RANDOMIZE never set */

static unsigned long mmap_base(unsigned long rnd, unsigned long task_size,
			       struct rlimit *rlim_stack)
{
	unsigned long gap = rlim_stack->rlim_cur;
	unsigned long pad = stack_maxrandom_size(task_size) + stack_guard_gap;
	unsigned long gap_min, gap_max;

	if (gap + pad > gap)
		gap += pad;

	gap_min = SIZE_128M;
	gap_max = (task_size / 6) * 5;

	if (gap < gap_min)
		gap = gap_min;
	else if (gap > gap_max)
		gap = gap_max;

	return PAGE_ALIGN(task_size - gap - rnd);
}

static unsigned long mmap_legacy_base(unsigned long rnd,
				      unsigned long task_size)
{
	return __TASK_UNMAPPED_BASE(task_size) + rnd;
}

static void arch_pick_mmap_base(unsigned long *base, unsigned long *legacy_base,
				unsigned long random_factor,
				unsigned long task_size,
				struct rlimit *rlim_stack)
{
	*legacy_base = mmap_legacy_base(random_factor, task_size);
	if (mmap_is_legacy())
		*base = *legacy_base;
	else
		*base = mmap_base(random_factor, task_size, rlim_stack);
}

void arch_pick_mmap_layout(struct mm_struct *mm, struct rlimit *rlim_stack)
{
	if (mmap_is_legacy())
		mm->get_unmapped_area = arch_get_unmapped_area;
	else
		mm->get_unmapped_area = arch_get_unmapped_area_topdown;

	/* arch_rnd always returns 0 */
	arch_pick_mmap_base(&mm->mmap_base, &mm->mmap_legacy_base, 0,
			    task_size_64bit(0), rlim_stack);
}

/* get_mmap_base, mmap_address_hint_valid, pfn_modify_allowed removed - never called or always true */
