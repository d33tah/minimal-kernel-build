 
#ifndef _ASM_X86_MEMTYPE_H
#define _ASM_X86_MEMTYPE_H

#include <linux/types.h>
#include <asm/pgtable_types.h>

/* pat_disable, pat_init, init_cache_modes removed - never called */

extern int memtype_reserve(u64 start, u64 end,
		enum page_cache_mode req_pcm, enum page_cache_mode *ret_pcm);
extern int memtype_free(u64 start, u64 end);

extern int memtype_reserve_io(resource_size_t start, resource_size_t end,
			enum page_cache_mode *pcm);

/* memtype_kernel_map_sync, memtype_free_io removed - never called */

bool x86_has_pat_wp(void);
enum page_cache_mode pgprot2cachemode(pgprot_t pgprot);

#endif  
