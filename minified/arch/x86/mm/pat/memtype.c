// Stubbed version of PAT (Page Attribute Table) memory type management
// Original: 591 LOC

#include <linux/seq_file.h>
#include <linux/memblock.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/pfn_t.h>
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
#include <asm/mtrr.h>
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

void pat_disable(const char *msg_reason)
{
}

void init_cache_modes(void)
{
}

/* pat_init removed - never called */

int memtype_reserve(u64 start, u64 end, enum page_cache_mode req_type,
		    enum page_cache_mode *new_type)
{
	if (new_type)
		*new_type = req_type;
	return 0;
}

int memtype_free(u64 start, u64 end)
{
	return 0;
}

int memtype_reserve_io(resource_size_t start, resource_size_t end,
		       enum page_cache_mode *pcm)
{
	if (pcm)
		*pcm = _PAGE_CACHE_MODE_UC_MINUS;
	return 0;
}

/* memtype_free_io removed - never called */

int memtype_kernel_map_sync(u64 base, unsigned long size,
			    enum page_cache_mode pcm)
{
	return 0;
}

/* track_pfn_copy, track_pfn_remap, track_pfn_insert, untrack_pfn removed - never called */

pgprot_t pgprot_writecombine(pgprot_t prot)
{
	return pgprot_noncached(prot);
}

/* pgprot_writethrough removed - never called */
