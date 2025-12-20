/* Minimal smap.h for assembly - the C code is in uaccess.h */
#ifndef _ASM_X86_SMAP_H
#define _ASM_X86_SMAP_H

#include <asm/nops.h>
#include <asm/cpufeatures.h>
#include <asm/alternative.h>

#define __ASM_CLAC	".byte 0x0f,0x01,0xca"
#define __ASM_STAC	".byte 0x0f,0x01,0xcb"

#ifdef __ASSEMBLY__

#define ASM_CLAC \
	ALTERNATIVE "", __ASM_CLAC, X86_FEATURE_SMAP

#define ASM_STAC \
	ALTERNATIVE "", __ASM_STAC, X86_FEATURE_SMAP

#else /* !__ASSEMBLY__ */


#define ASM_CLAC \
	ALTERNATIVE("", __ASM_CLAC, X86_FEATURE_SMAP)
#define ASM_STAC \
	ALTERNATIVE("", __ASM_STAC, X86_FEATURE_SMAP)

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_SMAP_H */
