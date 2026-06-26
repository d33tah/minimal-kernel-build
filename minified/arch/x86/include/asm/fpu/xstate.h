 
#ifndef __ASM_X86_XSAVE_H
#define __ASM_X86_XSAVE_H

#include <linux/uaccess.h>
#include <linux/types.h>

#include <asm/processor.h>
#include <asm/fpu/api.h>
#include <asm/user.h>

 



#define FXSAVE_SIZE	512



 
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

 

 

#define XFEATURE_MASK_FPSTATE	(XFEATURE_MASK_USER_RESTORE | \
				 XFEATURE_MASK_SUPERVISOR_SUPPORTED)

 

extern u64 xstate_fx_sw_bytes[USER_XSTATE_FX_SW_WORDS];





#endif
