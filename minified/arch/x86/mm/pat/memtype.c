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

#include "../mm_internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "" fmt

void pat_disable(const char *msg_reason) { }

void init_cache_modes(void) { }

void track_pfn_insert(struct vm_area_struct *vma, pgprot_t *prot, pfn_t pfn) { }

void untrack_pfn(struct vm_area_struct *vma, unsigned long pfn,
		 unsigned long size) { }
