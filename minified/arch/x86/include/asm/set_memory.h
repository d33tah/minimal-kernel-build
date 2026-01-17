 
#ifndef _ASM_X86_SET_MEMORY_H
#define _ASM_X86_SET_MEMORY_H

#include <linux/mm.h>
#include <asm/page.h>
/* Inlined from asm-generic/set_memory.h */
int set_memory_ro(unsigned long addr, int numpages);
int set_memory_rw(unsigned long addr, int numpages);
/* set_memory_x removed - never called */
int set_memory_nx(unsigned long addr, int numpages);

 

/* __set_memory_prot, _set_memory_uc, _set_memory_wc, _set_memory_wt, _set_memory_wb, set_memory_uc, set_memory_wc, set_memory_wb, set_memory_np_noalias, set_memory_4k, set_memory_nonglobal, set_memory_global, set_memory_np, set_memory_encrypted, set_memory_decrypted, set_direct_map_invalid_noflush, set_direct_map_default_noflush removed - never called */
int set_pages_ro(struct page *page, int numpages);

#endif  
