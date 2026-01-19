#ifndef _ASM_X86_SET_MEMORY_H
#define _ASM_X86_SET_MEMORY_H

#include <linux/mm.h>
#include <asm/page.h>

/* All set_memory functions inlined from pat/set_memory.c - all stubs return 0 */
static inline int set_memory_ro(unsigned long addr, int numpages) { return 0; }
static inline int set_memory_rw(unsigned long addr, int numpages) { return 0; }
static inline int set_memory_nx(unsigned long addr, int numpages) { return 0; }
static inline int set_pages_ro(struct page *page, int numpages) { return 0; }

#endif  
