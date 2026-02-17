#ifndef __ASM_X86_XSAVE_H
#define __ASM_X86_XSAVE_H

#include <linux/uaccess.h>
#include <linux/types.h>

#include <asm/processor.h>
#include <asm/fpu/api.h>

#define XFEATURE_MASK_USER_DYNAMIC	XFEATURE_MASK_XTILE_DATA

#define XFEATURE_MASK_FPSTATE	0x4FF

#endif
