// Stubbed set_memory.c for minimal kernel
// Original: 1,618 LOC
// Stubbed: ~50 LOC
// Date: 2025-11-21 12:50

#include <linux/highmem.h>
#include <linux/memblock.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/pfn.h>
#include <linux/percpu.h>
#include <linux/gfp.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/vmstat.h>
#include <linux/kernel.h>
#include <linux/cc_platform.h>
#include <linux/set_memory.h>
#include <asm/e820/api.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <linux/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/proto.h>
#include <asm/memtype.h>

// Stub: cache flushing
void clflush_cache_range(void *vaddr, unsigned int size) { }

// Stub: memory protection functions - all return success
/* __set_memory_prot removed - never called */
/* _set_memory_uc, set_memory_uc, _set_memory_wc, set_memory_wc, _set_memory_wt, _set_memory_wb, set_memory_wb, set_memory_np_noalias removed - never called */
int set_memory_x(unsigned long addr, int numpages) { return 0; }
int set_memory_nx(unsigned long addr, int numpages) { return 0; }
int set_memory_ro(unsigned long addr, int numpages) { return 0; }
int set_memory_rw(unsigned long addr, int numpages) { return 0; }
int set_memory_np(unsigned long addr, int numpages) { return 0; }
/* set_memory_4k, set_memory_nonglobal, set_memory_global removed - never called */
int set_memory_encrypted(unsigned long addr, int numpages) { return 0; }
int set_memory_decrypted(unsigned long addr, int numpages) { return 0; }

/* set_pages_uc, set_pages_array_uc, set_pages_array_wc, set_pages_wb, set_pages_array_wb removed - never called */
int set_pages_ro(struct page *page, int numpages) { return 0; }
/* set_pages_rw removed - never called */

// Stub: direct map manipulation
int set_direct_map_invalid_noflush(struct page *page) { return 0; }
int set_direct_map_default_noflush(struct page *page) { return 0; }

/* kernel_page_present, kernel_map_pages_in_pgd, kernel_unmap_pages_in_pgd removed - never called */
