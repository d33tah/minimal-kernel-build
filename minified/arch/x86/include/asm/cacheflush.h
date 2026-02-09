 
#ifndef _ASM_X86_CACHEFLUSH_H
#define _ASM_X86_CACHEFLUSH_H

#include <linux/mm.h>

/* Inlined from asm-generic/cacheflush.h */
struct page;

/* flush_cache_all, flush_cache_mm, flush_cache_page removed - unused */
/* flush_dcache_mmap_lock, flush_dcache_mmap_unlock removed - unused */
/* flush_icache_page, flush_cache_vmap, flush_cache_vunmap removed - unused */

/* flush_cache_range removed - empty stub on x86, calls eliminated */

/* flush_dcache_page called from highmem.h - keeping empty stub */
static inline void flush_dcache_page(struct page *page) { }
/* ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE removed - unused */

#include <asm/special_insns.h>

/* clflush_cache_range removed - unused */

#endif  
