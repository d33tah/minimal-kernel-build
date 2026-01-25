 
 
#ifndef _ASM_X86_MACH_DEFAULT_MACH_TRAPS_H
#define _ASM_X86_MACH_DEFAULT_MACH_TRAPS_H

#include <asm/mc146818rtc.h>

#define NMI_REASON_PORT		0x61

#define NMI_REASON_SERR		0x80
#define NMI_REASON_IOCHK	0x40
#define NMI_REASON_MASK		(NMI_REASON_SERR | NMI_REASON_IOCHK)

#define NMI_REASON_CLEAR_SERR	0x04
#define NMI_REASON_CLEAR_IOCHK	0x08
#define NMI_REASON_CLEAR_MASK	0x0f

static inline unsigned char default_get_nmi_reason(void)
{
	return inb(NMI_REASON_PORT);
}

/* reassert_nmi inlined at single call site in nmi.c */

#endif  
