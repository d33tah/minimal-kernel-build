 
#ifndef _ASM_X86_SPECCTRL_H_
#define _ASM_X86_SPECCTRL_H_

#include <linux/thread_info.h>
#include <asm/nospec-branch.h>

/* x86_virt_spec_ctrl, x86_spec_ctrl_set_guest, x86_spec_ctrl_restore_host removed - unused in minimal kernel */

/* ssbd_tif_to_spec_ctrl / ssbd_tif_to_amd_ls_cfg (+ x86_amd_ls_cfg_base /
 * x86_amd_ls_cfg_ssbd_mask externs) removed - their only consumers were the
 * AMD SSBD helpers in process.c, now folded out (feature absent on boot CPU). */

/* stibp_tif_to_spec_ctrl, ssbd_spec_ctrl_to_tif, stibp_spec_ctrl_to_tif removed - unused */

/* speculative_store_bypass_ht_init removed - unused */

#endif
