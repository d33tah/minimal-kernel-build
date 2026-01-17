 
#ifndef _ASM_X86_SPECCTRL_H_
#define _ASM_X86_SPECCTRL_H_

#include <linux/thread_info.h>
#include <asm/nospec-branch.h>

/* x86_virt_spec_ctrl, x86_spec_ctrl_set_guest, x86_spec_ctrl_restore_host removed - unused in minimal kernel */

extern u64 x86_amd_ls_cfg_base;
extern u64 x86_amd_ls_cfg_ssbd_mask;

static inline u64 ssbd_tif_to_spec_ctrl(u64 tifn)
{
	BUILD_BUG_ON(TIF_SSBD < SPEC_CTRL_SSBD_SHIFT);
	return (tifn & _TIF_SSBD) >> (TIF_SSBD - SPEC_CTRL_SSBD_SHIFT);
}

/* ssbd_spec_ctrl_to_tif, stibp_spec_ctrl_to_tif, stibp_tif_to_spec_ctrl removed - unused */

static inline u64 ssbd_tif_to_amd_ls_cfg(u64 tifn)
{
	return (tifn & _TIF_SSBD) ? x86_amd_ls_cfg_ssbd_mask : 0ULL;
}

/* speculative_store_bypass_ht_init removed - unused */

extern void speculation_ctrl_update(unsigned long tif);

#endif
