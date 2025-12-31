 
#ifndef _ASM_X86_CACHEFLUSH_H
#define _ASM_X86_CACHEFLUSH_H

#include <linux/mm.h>

/* Inlined from asm-generic/cacheflush.h */
struct mm_struct;
struct vm_area_struct;
struct page;
struct address_space;

/* flush_cache_all, flush_cache_mm, flush_cache_page removed - unused */
/* flush_dcache_mmap_lock, flush_dcache_mmap_unlock removed - unused */
/* flush_icache_page, flush_cache_vmap, flush_cache_vunmap removed - unused */

/* flush_cache_range called but is empty stub - keeping */
static inline void flush_cache_range(struct vm_area_struct *vma,
				     unsigned long start, unsigned long end) { }

/* flush_dcache_page called but is empty stub - keeping */
static inline void flush_dcache_page(struct page *page) { }
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 0

#include <asm/special_insns.h>

/* clflush_cache_range removed - unused */

#endif  
