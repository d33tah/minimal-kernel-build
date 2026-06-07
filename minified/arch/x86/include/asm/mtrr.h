 
#ifndef _ASM_X86_MTRR_H
#define _ASM_X86_MTRR_H

#include <uapi/asm/mtrr.h>

void mtrr_bp_init(void);

#define mtrr_save_fixed_ranges(arg) do {} while (0)
#define mtrr_save_state() do {} while (0)
static inline int mtrr_trim_uncached_memory(unsigned long end_pfn)
{
	return 0;
}
#define mtrr_ap_init() do {} while (0)
#define set_mtrr_aps_delayed_init() do {} while (0)
#define mtrr_aps_init() do {} while (0)
#define mtrr_bp_restore() do {} while (0)


 
#define MTRR_STATE_MTRR_FIXED_ENABLED	0x01
#define MTRR_STATE_MTRR_ENABLED		0x02

#endif  
