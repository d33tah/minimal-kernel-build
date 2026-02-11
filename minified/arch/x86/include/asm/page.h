 
#ifndef _ASM_X86_PAGE_H
#define _ASM_X86_PAGE_H

#include <linux/types.h>

#ifdef __KERNEL__

#include <asm/page_types.h>

/* --- 2025-12-08 00:22 --- Inlined from page_32.h */
/* page_32_types.h inlined */
#ifndef _ASM_X86_PAGE_32_DEFS_H
#define _ASM_X86_PAGE_32_DEFS_H

#include <linux/const.h>

#define __PAGE_OFFSET_BASE	_AC(CONFIG_PAGE_OFFSET, UL)
#define __PAGE_OFFSET		__PAGE_OFFSET_BASE

#define THREAD_SIZE_ORDER	1
#define THREAD_SIZE		(PAGE_SIZE << THREAD_SIZE_ORDER)

#define IRQ_STACK_SIZE		THREAD_SIZE

#define __PHYSICAL_MASK_SHIFT	32

#define TASK_SIZE		__PAGE_OFFSET
#define TASK_SIZE_LOW		TASK_SIZE
#define TASK_SIZE_MAX		TASK_SIZE
#define DEFAULT_MAP_WINDOW	TASK_SIZE
#define STACK_TOP		TASK_SIZE
#define STACK_TOP_MAX		STACK_TOP

#define KERNEL_IMAGE_SIZE	(512 * 1024 * 1024)

#ifndef __ASSEMBLY__

extern unsigned int __VMALLOC_RESERVE;

extern void find_low_pfn_range(void);

#endif

#endif /* _ASM_X86_PAGE_32_DEFS_H */

#ifndef __ASSEMBLY__

#define __phys_addr_nodebug(x)	((x) - PAGE_OFFSET)
#define __phys_addr(x)		__phys_addr_nodebug(x)
#define __phys_addr_symbol(x)	__phys_addr(x)
#define __phys_reloc_hide(x)	RELOC_HIDE((x), 0)

#define pfn_valid(pfn)		((pfn) < max_mapnr)

#include <linux/string.h>

static inline void clear_page(void *page)
{
	memset(page, 0, PAGE_SIZE);
}

static inline void copy_page(void *to, void *from)
{
	memcpy(to, from, PAGE_SIZE);
}
/* end page_32.h */

struct page;

#include <linux/range.h>

/* clear_user_page removed - never called */

static inline void copy_user_page(void *to, void *from, unsigned long vaddr,
				  struct page *topage)
{
	copy_page(to, from);
}

#define alloc_zeroed_user_highpage_movable(vma, vaddr) \
	alloc_page_vma(GFP_HIGHUSER_MOVABLE | __GFP_ZERO, vma, vaddr)
#define __HAVE_ARCH_ALLOC_ZEROED_USER_HIGHPAGE_MOVABLE

#ifndef __pa
#define __pa(x)		__phys_addr((unsigned long)(x))
#endif

/* __pa_nodebug removed - no callers */

#define __pa_symbol(x) \
	__phys_addr_symbol(__phys_reloc_hide((unsigned long)(x)))

#ifndef __va
#define __va(x)			((void *)((unsigned long)(x)+PAGE_OFFSET))
#endif

/* __boot_va, __boot_pa removed - unused */

#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
/* pfn_to_kaddr, __virt_addr_valid, virt_addr_valid removed - unused */

#endif

/* Inlined from asm-generic/memory_model.h */
#include <linux/pfn.h>
#ifndef ARCH_PFN_OFFSET
#define ARCH_PFN_OFFSET		(0UL)
#endif
#define __pfn_to_page(pfn)	(mem_map + ((pfn) - ARCH_PFN_OFFSET))
#define __page_to_pfn(page)	((unsigned long)((page) - mem_map) + \
				 ARCH_PFN_OFFSET)
#define page_to_pfn __page_to_pfn
#define pfn_to_page __pfn_to_page
/* __phys_to_pfn, __pfn_to_phys, HAVE_ARCH_HUGETLB_UNMAPPED_AREA removed - unused */

/* --- 2026-01-26 00:50 --- Inlined from asm-generic/getorder.h */
#ifndef __ASSEMBLY__
#include <linux/log2.h>
static __always_inline __attribute_const__ int get_order(unsigned long size)
{
	if (__builtin_constant_p(size)) {
		if (!size)
			return BITS_PER_LONG - PAGE_SHIFT;

		if (size < (1UL << PAGE_SHIFT))
			return 0;

		return ilog2((size) - 1) - PAGE_SHIFT + 1;
	}

	size--;
	size >>= PAGE_SHIFT;
	return fls(size); /* BITS_PER_LONG == 32 */
}
#endif
/* end getorder.h */

#endif
#endif  
