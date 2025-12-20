// Stubbed version of PAT (Page Attribute Table) memory type management
// Original: 591 LOC

#include <linux/seq_file.h>
#include <linux/memblock.h>
#include <linux/debugfs.h>
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

#define dprintk(fmt, arg...) \
	do { if (pat_debug_enable) pr_info("x86/PAT: " fmt, ##arg); } while (0)

struct memtype {
	u64			start;
	u64			end;
	u64			subtree_max_end;
	enum page_cache_mode	type;
	struct rb_node		rb;
};

static inline char *cattr_name(enum page_cache_mode pcm)
{
	switch (pcm) {
	case _PAGE_CACHE_MODE_UC:		return "uncached";
	case _PAGE_CACHE_MODE_UC_MINUS:		return "uncached-minus";
	case _PAGE_CACHE_MODE_WB:		return "write-back";
	case _PAGE_CACHE_MODE_WC:		return "write-combining";
	case _PAGE_CACHE_MODE_WT:		return "write-through";
	case _PAGE_CACHE_MODE_WP:		return "write-protected";
	default:				return "broken";
	}
}

static inline int memtype_check_insert(struct memtype *entry_new,
				       enum page_cache_mode *new_type)
{ return 0; }
static inline struct memtype *memtype_erase(u64 start, u64 end)
{ return NULL; }
static inline struct memtype *memtype_lookup(u64 addr)
{ return NULL; }
static inline int memtype_copy_nth_element(struct memtype *out, loff_t pos)
{ return 0; }
/* --- End inlined memtype.h --- */

#include "../mm_internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "" fmt

void pat_disable(const char *msg_reason) { }


bool pat_enabled(void)
{
	return false;
}

void init_cache_modes(void) { }

void pat_init(void) { }

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

bool pat_pfn_immune_to_uc_mtrr(unsigned long pfn)
{
	return false;
}

int memtype_reserve_io(resource_size_t start, resource_size_t end,
		       enum page_cache_mode *pcm)
{
	if (pcm)
		*pcm = _PAGE_CACHE_MODE_UC_MINUS;
	return 0;
}

void memtype_free_io(resource_size_t start, resource_size_t end) { }


int memtype_kernel_map_sync(u64 base, unsigned long size,
			     enum page_cache_mode pcm)
{
	return 0;
}

int track_pfn_copy(struct vm_area_struct *vma)
{
	return 0;
}

int track_pfn_remap(struct vm_area_struct *vma, pgprot_t *prot,
		    unsigned long pfn, unsigned long addr, unsigned long size)
{
	return 0;
}

void track_pfn_insert(struct vm_area_struct *vma, pgprot_t *prot, pfn_t pfn) { }

void untrack_pfn(struct vm_area_struct *vma, unsigned long pfn,
		 unsigned long size) { }


pgprot_t pgprot_writecombine(pgprot_t prot)
{
	return pgprot_noncached(prot);
}

pgprot_t pgprot_writethrough(pgprot_t prot)
{
	return pgprot_noncached(prot);
}
