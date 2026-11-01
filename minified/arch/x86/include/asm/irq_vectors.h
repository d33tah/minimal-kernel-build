/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IRQ_VECTORS_H
#define _ASM_X86_IRQ_VECTORS_H

/*
 * IDT vectors usable for external interrupt sources start at 0x20.
 * (0x80 is the syscall vector, 0x30-0x3f are for ISA)
 */
#define FIRST_EXTERNAL_VECTOR 0x20

#define IA32_SYSCALL_VECTOR 0x80

/*
 * Vectors 0x30-0x3f are used for ISA interrupts.
 *   round up to the next 16-vector boundary
 */
#define ISA_IRQ_VECTOR(irq) (((FIRST_EXTERNAL_VECTOR + 16) & ~15) + irq)

#define NR_VECTORS 256

#define FIRST_SYSTEM_VECTOR NR_VECTORS

#define NR_EXTERNAL_VECTORS (FIRST_SYSTEM_VECTOR - FIRST_EXTERNAL_VECTOR)

/*
 * Size the maximum number of interrupts.
 *
 * If the irq_desc[] array has a sparse layout, we can size things
 * generously - it scales up linearly with the maximum number of CPUs,
 * and the maximum number of IO-APICs, whichever is higher.
 *
 * In other cases we size more conservatively, to not create too large
 * static arrays.
 */

#define NR_IRQS_LEGACY 16

#define NR_IRQS NR_IRQS_LEGACY

#endif
