 
#ifndef _ASM_X86_MTRR_H
#define _ASM_X86_MTRR_H

#include <uapi/asm/mtrr.h>

void mtrr_bp_init(void);

 
static inline u8 mtrr_type_lookup(u64 addr, u64 end, u8 *uniform)
{
	 
	return MTRR_TYPE_INVALID;
}
#define mtrr_save_fixed_ranges(arg) do {} while (0)
#define mtrr_save_state() do {} while (0)
static inline int mtrr_add(unsigned long base, unsigned long size,
			   unsigned int type, bool increment)
{
    return -ENODEV;
}
static inline int mtrr_add_page(unsigned long base, unsigned long size,
				unsigned int type, bool increment)
{
    return -ENODEV;
}
static inline int mtrr_del(int reg, unsigned long base, unsigned long size)
{
    return -ENODEV;
}
static inline int mtrr_del_page(int reg, unsigned long base, unsigned long size)
{
    return -ENODEV;
}
static inline int mtrr_trim_uncached_memory(unsigned long end_pfn)
{
	return 0;
}
static inline void mtrr_centaur_report_mcr(int mcr, u32 lo, u32 hi)
{
}
#define mtrr_ap_init() do {} while (0)
#define set_mtrr_aps_delayed_init() do {} while (0)
#define mtrr_aps_init() do {} while (0)
#define mtrr_bp_restore() do {} while (0)


 
#define MTRR_STATE_MTRR_FIXED_ENABLED	0x01
#define MTRR_STATE_MTRR_ENABLED		0x02

#endif  
