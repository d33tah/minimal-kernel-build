
#ifndef _ASM_X86_PAGE_H
#define _ASM_X86_PAGE_H

#include <linux/types.h>

#include <asm/page_types.h>

/* --- 2025-12-08 00:22 --- Inlined from page_32.h */

#ifndef __ASSEMBLY__

#define __phys_addr_nodebug(x) ((x) - PAGE_OFFSET)
#define __phys_addr(x) __phys_addr_nodebug(x)
#define __phys_addr_symbol(x) __phys_addr(x)
#define __phys_reloc_hide(x) RELOC_HIDE((x), 0)

#define pfn_valid(pfn) ((pfn) < max_mapnr)

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

static inline void copy_user_page(void *to, void *from, unsigned long vaddr,
				  struct page *topage)
{
	copy_page(to, from);
}

#define alloc_zeroed_user_highpage_movable(vma, vaddr) \
	alloc_page_vma(GFP_HIGHUSER_MOVABLE | __GFP_ZERO, vma, vaddr)

#define __pa(x) __phys_addr((unsigned long)(x))

#define __pa_nodebug(x) __phys_addr_nodebug((unsigned long)(x))

#define __pa_symbol(x) __phys_addr_symbol(__phys_reloc_hide((unsigned long)(x)))

#define __va(x) ((void *)((unsigned long)(x) + PAGE_OFFSET))

/* __boot_va, __boot_pa removed - unused */

#define virt_to_page(kaddr) pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)

#endif

/* Inlined from asm-generic/memory_model.h */
#include <linux/pfn.h>
#define ARCH_PFN_OFFSET (0UL)
#define __pfn_to_page(pfn) (mem_map + ((pfn) - ARCH_PFN_OFFSET))
#define __page_to_pfn(page) \
	((unsigned long)((page) - mem_map) + ARCH_PFN_OFFSET)
#define page_to_pfn __page_to_pfn
#define pfn_to_page __pfn_to_page

#include <asm-generic/getorder.h>

#endif
