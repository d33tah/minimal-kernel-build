 
#ifndef _ASM_X86_MEMTYPE_H
#define _ASM_X86_MEMTYPE_H

#include <linux/types.h>
#include <asm/pgtable_types.h>

/* pat_disable, pat_init, init_cache_modes removed - never called */

extern int memtype_reserve(u64 start, u64 end,
		enum page_cache_mode req_pcm, enum page_cache_mode *ret_pcm);

/* memtype_reserve_io, memtype_free, memtype_kernel_map_sync, memtype_free_io removed */

/* x86_has_pat_wp removed - never called */
enum page_cache_mode pgprot2cachemode(pgprot_t pgprot);

#endif  
