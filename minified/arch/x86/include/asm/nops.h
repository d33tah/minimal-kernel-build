/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_NOPS_H
#define _ASM_X86_NOPS_H

#include <asm/asm.h>

/*
 * Define nops for use with alternative() and for tracing.
 */


/*
 * Generic 32bit nops from GAS:
 *
 * 1: nop
 * 2: movl %esi,%esi
 * 3: leal 0x0(%esi),%esi
 * 4: leal 0x0(%esi,%eiz,1),%esi
 * 5: leal %ds:0x0(%esi,%eiz,1),%esi
 * 6: leal 0x0(%esi),%esi
 * 7: leal 0x0(%esi,%eiz,1),%esi
 * 8: leal %ds:0x0(%esi,%eiz,1),%esi
 *
 * Except 5 and 8, which are DS prefixed 4 and 7 resp, where GAS would emit 2
 * nop instructions.
 */
#define BYTES_NOP1	0x90
#define BYTES_NOP2	0x89,0xf6
#define BYTES_NOP3	0x8d,0x76,0x00
#define BYTES_NOP4	0x8d,0x74,0x26,0x00
#define BYTES_NOP5	0x3e,BYTES_NOP4
#define BYTES_NOP6	0x8d,0xb6,0x00,0x00,0x00,0x00
#define BYTES_NOP7	0x8d,0xb4,0x26,0x00,0x00,0x00,0x00
#define BYTES_NOP8	0x3e,BYTES_NOP7


#define ASM_NOP1 _ASM_BYTES(BYTES_NOP1)
#define ASM_NOP2 _ASM_BYTES(BYTES_NOP2)
#define ASM_NOP3 _ASM_BYTES(BYTES_NOP3)
#define ASM_NOP4 _ASM_BYTES(BYTES_NOP4)
#define ASM_NOP5 _ASM_BYTES(BYTES_NOP5)
#define ASM_NOP6 _ASM_BYTES(BYTES_NOP6)
#define ASM_NOP7 _ASM_BYTES(BYTES_NOP7)
#define ASM_NOP8 _ASM_BYTES(BYTES_NOP8)

#define ASM_NOP_MAX 8

#ifndef __ASSEMBLY__
extern const unsigned char * const x86_nops[];
#endif

#endif /* _ASM_X86_NOPS_H */
