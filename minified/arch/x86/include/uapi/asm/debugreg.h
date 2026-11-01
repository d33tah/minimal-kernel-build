/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_X86_DEBUGREG_H
#define _UAPI_ASM_X86_DEBUGREG_H

/* Define a few things for the status register.  We can use this to determine
   which debugging register was responsible for the trap.  The other bits
   are either reserved or not of interest to us. */

/* Define reserved bits in DR6 which are always set to 1 */
#define DR6_RESERVED (0xFFFF0FF0)

#define DR_STEP (0x4000) /* single-step */

#endif
