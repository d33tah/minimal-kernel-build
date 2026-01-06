 
#ifndef __ASM_X86_XSAVE_H
#define __ASM_X86_XSAVE_H

#include <linux/uaccess.h>
#include <linux/types.h>

#include <asm/processor.h>
#include <asm/fpu/api.h>
#include <asm/user.h>

/* XFEATURE_MASK_EXTEND removed - unused */

#define FXSAVE_SIZE	512
/* XSAVE_HDR_SIZE, XSAVE_HDR_OFFSET removed - never used */

#define XFEATURE_MASK_USER_SUPPORTED (XFEATURE_MASK_FP | \
				      XFEATURE_MASK_SSE | \
				      XFEATURE_MASK_YMM | \
				      XFEATURE_MASK_OPMASK | \
				      XFEATURE_MASK_ZMM_Hi256 | \
				      XFEATURE_MASK_Hi16_ZMM	 | \
				      XFEATURE_MASK_PKRU | \
				      XFEATURE_MASK_BNDREGS | \
				      XFEATURE_MASK_BNDCSR | \
				      XFEATURE_MASK_XTILE)

 
#define XFEATURE_MASK_USER_RESTORE	\
	(XFEATURE_MASK_USER_SUPPORTED & ~XFEATURE_MASK_PKRU)

 
#define XFEATURE_MASK_USER_DYNAMIC	XFEATURE_MASK_XTILE_DATA

 
#define XFEATURE_MASK_SUPERVISOR_SUPPORTED (XFEATURE_MASK_PASID)

 
#define XFEATURE_MASK_INDEPENDENT (XFEATURE_MASK_LBR)

 
#define XFEATURE_MASK_SUPERVISOR_UNSUPPORTED (XFEATURE_MASK_PT)
/* XFEATURE_MASK_SUPERVISOR_ALL removed - never used */

#define XFEATURE_MASK_FPSTATE	(XFEATURE_MASK_USER_RESTORE | \
				 XFEATURE_MASK_SUPERVISOR_SUPPORTED)

 
#define XFEATURE_MASK_SIGFRAME_INITOPT	(XFEATURE_MASK_XTILE | \
					 XFEATURE_MASK_USER_DYNAMIC)

/* xstate_fx_sw_bytes removed - never used */

int xfeature_size(int xfeature_nr);

void xsaves(struct xregs_state *xsave, u64 mask);
void xrstors(struct xregs_state *xsave, u64 mask);

int xfd_enable_feature(u64 xfd_err);


static __always_inline __pure bool fpu_state_size_dynamic(void)
{
	return false;
}

#endif
