 
#ifndef _ASM_X86_CACHEFLUSH_H
#define _ASM_X86_CACHEFLUSH_H

#include <linux/mm.h>

/* Inlined from asm-generic/cacheflush.h */
struct mm_struct;
struct vm_area_struct;
struct page;
struct address_space;

/* flush_cache_all removed - unused */

#ifndef flush_cache_range
static inline void flush_cache_range(struct vm_area_struct *vma,
				     unsigned long start, unsigned long end) { }
#endif

/* flush_cache_page removed - no-op on x86, all callers dropped */

static inline void flush_dcache_page(struct page *page) { }

/* flush_icache_range removed - unused */
/* flush_icache_user_range removed - unused (macro for flush_icache_range) */

#ifndef flush_icache_page
static inline void flush_icache_page(struct vm_area_struct *vma,
				     struct page *page) { }
#endif

#include <asm/special_insns.h>

/* clflush_cache_range removed - unused */

#endif  
