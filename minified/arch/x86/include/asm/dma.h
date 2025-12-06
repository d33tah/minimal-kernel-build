/* SPDX-License-Identifier: GPL-2.0 */
/* Minimal stub - only MAX_DMA_ADDRESS is actually used */

#ifndef _ASM_X86_DMA_H
#define _ASM_X86_DMA_H

/* Only define what's actually used */
#define MAX_DMA_ADDRESS      (PAGE_OFFSET + 0x1000000)

#endif /* _ASM_X86_DMA_H */
