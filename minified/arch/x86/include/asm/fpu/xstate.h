
#ifndef __ASM_X86_XSAVE_H
#define __ASM_X86_XSAVE_H

#include <linux/uaccess.h>
#include <linux/types.h>

#include <asm/processor.h>
#include <asm/fpu/api.h>
#include <asm/user.h>

/*
 * XFEATURE_MASK_{USER_SUPPORTED,USER_RESTORE,SUPERVISOR_SUPPORTED,FPSTATE}
 * removed: restore_fpregs_from_fpstate() ignores its restore mask (no XSAVE
 * on this build), so the composite masks and their per-component base masks
 * had no remaining consumer.
 */

#endif
