#ifndef _ASM_X86_MSHYPERV_H
#define _ASM_X86_MSHYPERV_H

/* Minimal stub - hypervisor support not needed for basic kernel */

#include <linux/types.h>

static inline bool hv_is_isolation_supported(void) { return false; }
static inline int hv_set_mem_host_visibility(unsigned long addr, int numpages, bool visible) { return 0; }

#endif
