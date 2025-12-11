 
#ifndef _ASM_X86_SET_MEMORY_H
#define _ASM_X86_SET_MEMORY_H

#include <linux/mm.h>
#include <asm/page.h>
/* Inlined from asm-generic/set_memory.h */
int set_memory_ro(unsigned long addr, int numpages);
int set_memory_rw(unsigned long addr, int numpages);
int set_memory_x(unsigned long addr, int numpages);
int set_memory_nx(unsigned long addr, int numpages);

 

int __set_memory_prot(unsigned long addr, int numpages, pgprot_t prot);
/* _set_memory_uc, _set_memory_wc, _set_memory_wt, _set_memory_wb, set_memory_uc, set_memory_wc, set_memory_wb, set_memory_np_noalias, set_memory_4k, set_memory_nonglobal, set_memory_global removed - never called */
int set_memory_np(unsigned long addr, int numpages);
int set_memory_encrypted(unsigned long addr, int numpages);
int set_memory_decrypted(unsigned long addr, int numpages);

/* set_pages_array_uc, set_pages_array_wc, set_pages_array_wb, set_pages_uc, set_pages_wb, set_pages_rw removed - never called */
int set_pages_ro(struct page *page, int numpages);

int set_direct_map_invalid_noflush(struct page *page);
int set_direct_map_default_noflush(struct page *page);
bool kernel_page_present(struct page *page);

extern int kernel_set_to_readonly;

#endif  
