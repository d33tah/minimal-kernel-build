
#ifndef _ASM_X86_CACHEFLUSH_H
#define _ASM_X86_CACHEFLUSH_H

#include <linux/mm.h>

/* Inlined from asm-generic/cacheflush.h */
struct vm_area_struct;
struct page;

/* flush_cache_all removed - unused */

/* flush_cache_page removed - no-op on x86, all callers dropped */

/* flush_icache_range removed - unused */
/* flush_icache_user_range removed - unused (macro for flush_icache_range) */

/* clflush_cache_range removed - unused */

#endif
