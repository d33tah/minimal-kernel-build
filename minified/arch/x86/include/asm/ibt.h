/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IBT_H
#define _ASM_X86_IBT_H

#include <linux/types.h>

/*
 * The rules for enabling IBT are:
 *
 *  - CC_HAS_IBT:         the toolchain supports it
 *  - X86_KERNEL_IBT:     it is selected in Kconfig
 *  - !__DISABLE_EXPORTS: this is regular kernel code
 *
 * Esp. that latter one is a bit non-obvious, but some code like compressed,
 * purgatory, realmode etc.. is built with custom CFLAGS that do not include
 * -fcf-protection=branch and things will go *bang*.
 *
 * When all the above are satisfied, HAS_KERNEL_IBT will be 1, otherwise 0.
 */

#define HAS_KERNEL_IBT	0

#ifndef __ASSEMBLY__

#define ASM_ENDBR

#define __noendbr

static inline bool is_endbr(u32 val) { return false; }

static inline u64 ibt_save(void) { return 0; }
static inline void ibt_restore(u64 save) { }

#else /* __ASSEMBLY__ */

#define ENDBR

#endif /* __ASSEMBLY__ */


#define ENDBR_INSN_SIZE		(4*HAS_KERNEL_IBT)

#endif /* _ASM_X86_IBT_H */
