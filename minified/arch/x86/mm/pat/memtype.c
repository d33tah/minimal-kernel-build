// Stubbed version of PAT (Page Attribute Table) memory type management
// Original: 591 LOC - all functions removed as dead code

/* All includes kept minimal for compilation */
#include <linux/kernel.h>
#include <linux/mm.h>

#include <asm/memtype.h>

#include "../mm_internal.h"

/* pat_debug_enable, dprintk, struct memtype, cattr_name,
   pat_disable, init_cache_modes, pat_init,
   memtype_reserve, memtype_free, memtype_reserve_io, memtype_free_io,
   memtype_kernel_map_sync, track_pfn_copy, track_pfn_remap, track_pfn_insert,
   untrack_pfn, pgprot_writecombine, pgprot_writethrough removed - never called */
