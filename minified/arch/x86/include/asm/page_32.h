 
#ifndef _ASM_X86_PAGE_32_H
#define _ASM_X86_PAGE_32_H

#include <asm/page_32_types.h>

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
#endif	 

#endif  
