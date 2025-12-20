 
#ifndef _ASM_X86_CACHEFLUSH_H
#define _ASM_X86_CACHEFLUSH_H

#include <linux/mm.h>

/* Inlined from asm-generic/cacheflush.h */
struct mm_struct;
struct vm_area_struct;
struct page;
struct address_space;

/* flush_cache_all removed - unused */

#ifndef flush_cache_mm
static inline void flush_cache_mm(struct mm_struct *mm) { }
#endif

#ifndef flush_cache_range
static inline void flush_cache_range(struct vm_area_struct *vma,
				     unsigned long start, unsigned long end) { }
#endif

#ifndef flush_cache_page
static inline void flush_cache_page(struct vm_area_struct *vma,
				    unsigned long vmaddr, unsigned long pfn) { }
#endif

#ifndef ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE
static inline void flush_dcache_page(struct page *page) { }
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 0
#endif

#ifndef flush_dcache_mmap_lock
static inline void flush_dcache_mmap_lock(struct address_space *mapping) { }
#endif

#ifndef flush_dcache_mmap_unlock
static inline void flush_dcache_mmap_unlock(struct address_space *mapping) { }
#endif

/* flush_icache_range removed - unused */
/* flush_icache_user_range removed - unused (macro for flush_icache_range) */

#ifndef flush_icache_page
static inline void flush_icache_page(struct vm_area_struct *vma,
				     struct page *page) { }
#endif

#ifndef flush_cache_vmap
static inline void flush_cache_vmap(unsigned long start, unsigned long end) { }
#endif

#ifndef flush_cache_vunmap
static inline void flush_cache_vunmap(unsigned long start, unsigned long end) { }
#endif

#include <asm/special_insns.h>

/* clflush_cache_range removed - unused */

#endif  
