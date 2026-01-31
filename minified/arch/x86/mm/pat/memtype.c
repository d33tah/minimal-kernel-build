// Stubbed version of PAT (Page Attribute Table) memory type management
// Original: 591 LOC

/* seq_file.h removed - header is empty */
#include <linux/memblock.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
/* pfn_t.h removed - not used */
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/rbtree.h>

#include <asm/cacheflush.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/x86_init.h>
#include <asm/fcntl.h>
#include <asm/e820/api.h>
/* mtrr.h removed - header is empty */
#include <asm/page.h>
#include <asm/msr.h>
#include <asm/memtype.h>
#include <asm/io.h>

/* --- Inlined from memtype.h (2025-12-08 01:55) --- */
int pat_debug_enable;

#define dprintk(fmt, arg...)                             \
	do {                                             \
		if (pat_debug_enable)                    \
			pr_info("x86/PAT: " fmt, ##arg); \
	} while (0)

struct memtype {
	u64 start;
	u64 end;
	u64 subtree_max_end;
	enum page_cache_mode type;
	struct rb_node rb;
};

/* cattr_name removed - never called */
#include "../mm_internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "" fmt

/* pat_disable, init_cache_modes, pat_init removed - never called/empty stubs */

int memtype_reserve(u64 start, u64 end, enum page_cache_mode req_type,
		    enum page_cache_mode *new_type)
{
	if (new_type)
		*new_type = req_type;
	return 0;
}

/* memtype_free, memtype_reserve_io, memtype_free_io, memtype_kernel_map_sync,
   track_pfn_copy, track_pfn_remap, track_pfn_insert, untrack_pfn removed - never called */

pgprot_t pgprot_writecombine(pgprot_t prot)
{
	return pgprot_noncached(prot);
}

/* pgprot_writethrough removed - never called */
