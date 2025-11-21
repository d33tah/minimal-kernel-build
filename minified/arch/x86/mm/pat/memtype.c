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

#include "memtype.h"
#include "../mm_internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "" fmt

static bool __read_mostly pat_disabled = true;

void pat_disable(const char *msg_reason) { }

static int __init nopat(char *str)
{
	return 0;
}
early_param("nopat", nopat);

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

pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
			       unsigned long size, pgprot_t vma_prot)
{
	return vma_prot;
}

int phys_mem_access_prot_allowed(struct file *file, unsigned long pfn,
				  unsigned long size, pgprot_t *vma_prot)
{
	return 1;
}

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

void untrack_pfn_moved(struct vm_area_struct *vma) { }

pgprot_t pgprot_writecombine(pgprot_t prot)
{
	return pgprot_noncached(prot);
}

pgprot_t pgprot_writethrough(pgprot_t prot)
{
	return pgprot_noncached(prot);
}
