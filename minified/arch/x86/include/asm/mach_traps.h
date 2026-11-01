/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Machine specific NMI handling for generic.
 *  Split out from traps.c by Osamu Tomita <tomita@cinet.co.jp>
 */
#ifndef _ASM_X86_MACH_DEFAULT_MACH_TRAPS_H
#define _ASM_X86_MACH_DEFAULT_MACH_TRAPS_H

#define NMI_REASON_PORT 0x61

#define NMI_REASON_SERR 0x80
#define NMI_REASON_IOCHK 0x40
#define NMI_REASON_MASK (NMI_REASON_SERR | NMI_REASON_IOCHK)

#define NMI_REASON_CLEAR_SERR 0x04
#define NMI_REASON_CLEAR_IOCHK 0x08
#define NMI_REASON_CLEAR_MASK 0x0f

static inline unsigned char default_get_nmi_reason(void)
{
	return inb(NMI_REASON_PORT);
}

static inline void reassert_nmi(void)
{
	outb(0x8f, 0x70);
	inb(0x71); /* dummy */
	outb(0x0f, 0x70);
	inb(0x71); /* dummy */
}

#endif /* _ASM_X86_MACH_DEFAULT_MACH_TRAPS_H */
