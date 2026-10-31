

#ifndef _ASM_X86_FIXMAP_H
#define _ASM_X86_FIXMAP_H

#ifndef __ASSEMBLY__
#include <linux/kernel.h>
#include <asm/page.h>
#include <asm/pgtable_types.h>
#include <linux/threads.h>

extern unsigned long __FIXADDR_TOP;
#define FIXADDR_TOP ((unsigned long)__FIXADDR_TOP)

enum fixed_addresses { FIX_HOLE, FIX_WP_TEST, __end_of_fixed_addresses };

#define FIXADDR_TOT_SIZE (__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_TOT_START (FIXADDR_TOP - FIXADDR_TOT_SIZE)

void native_set_fixmap(unsigned idx, phys_addr_t phys, pgprot_t flags);

static inline void __set_fixmap(enum fixed_addresses idx, phys_addr_t phys,
				pgprot_t flags)
{
	native_set_fixmap(idx, phys, flags);
}

/* Inlined from asm-generic/fixmap.h */
#include <linux/bug.h>
#include <linux/mm_types.h>

#define __fix_to_virt(x) (FIXADDR_TOP - ((x) << PAGE_SHIFT))

static __always_inline unsigned long fix_to_virt(const unsigned int idx)
{
	BUILD_BUG_ON(idx >= __end_of_fixed_addresses);
	return __fix_to_virt(idx);
}

#define clear_fixmap(idx) __set_fixmap(idx, 0, __pgprot(0))

#endif
#endif
