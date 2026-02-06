/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * atomic64_t for 386/486
 *
 * Copyright © 2010  Luca Barbieri
 */

#include <linux/linkage.h>
#include <asm/alternative.h>

/* if you want SMP support, implement these with real spinlocks */
.macro IRQ_SAVE reg
	pushfl
	cli
.endm

.macro IRQ_RESTORE reg
	popfl
.endm

#define BEGIN_IRQ_SAVE(op) \
.macro endp; \
SYM_FUNC_END(atomic64_##op##_386); \
.purgem endp; \
.endm; \
SYM_FUNC_START(atomic64_##op##_386); \
	IRQ_SAVE v;

#define ENDP endp

#define RET_IRQ_RESTORE \
	IRQ_RESTORE v; \
	RET

#define v %ecx
BEGIN_IRQ_SAVE(read)
	movl  (v), %eax
	movl 4(v), %edx
	RET_IRQ_RESTORE
ENDP
#undef v

#define v %esi
BEGIN_IRQ_SAVE(set)
	movl %ebx,  (v)
	movl %ecx, 4(v)
	RET_IRQ_RESTORE
ENDP
#undef v

#define v %ecx
BEGIN_IRQ_SAVE(add_return)
	addl  (v), %eax
	adcl 4(v), %edx
	movl %eax,  (v)
	movl %edx, 4(v)
	RET_IRQ_RESTORE
ENDP
#undef v

#define v %esi
BEGIN_IRQ_SAVE(inc_return)
	movl  (v), %eax
	movl 4(v), %edx
	addl $1, %eax
	adcl $0, %edx
	movl %eax,  (v)
	movl %edx, 4(v)
	RET_IRQ_RESTORE
ENDP
#undef v

/* Removed unused assembly functions: xchg, add, sub, sub_return,
 * inc, dec, dec_return, add_unless, inc_not_zero, dec_if_positive
 * (~120 LOC) */
