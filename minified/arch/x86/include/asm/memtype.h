 
#ifndef _ASM_X86_MEMTYPE_H
#define _ASM_X86_MEMTYPE_H

#include <linux/types.h>
#include <asm/pgtable_types.h>

extern void pat_disable(const char *reason);
extern void init_cache_modes(void);

#endif
