/* SPDX-License-Identifier: GPL-2.0 */
/*
 * GCC stack protector support.
 *
 * Stack protector works by putting predefined pattern at the start of
 * the stack frame and verifying that it hasn't been overwritten when
 * returning from the function.  The pattern is called stack canary
 * and unfortunately gcc historically required it to be at a fixed offset
 * from the percpu segment base.  On x86_64, the offset is 40 bytes.
 *
 * The same segment is shared by percpu area and stack canary.  On
 * x86_64, percpu symbols are zero based and %gs (64-bit) points to the
 * base of percpu area.  The first occupant of the percpu area is always
 * fixed_percpu_data which contains stack_canary at the appropriate
 * offset.  On x86_32, the stack canary is just a regular percpu
 * variable.
 *
 * Putting percpu data in %fs on 32-bit is a minor optimization compared to
 * using %gs.  Since 32-bit userspace normally has %fs == 0, we are likely
 * to load 0 into %fs on exit to usermode, whereas with percpu data in
 * %gs, we are likely to load a non-null %gs on return to user mode.
 *
 * Once we are willing to require GCC 8.1 or better for 64-bit stackprotector
 * support, we can remove some of this complexity.
 */

#ifndef _ASM_STACKPROTECTOR_H
#define _ASM_STACKPROTECTOR_H 1


/* dummy boot_init_stack_canary() is defined in linux/stackprotector.h */

static inline void cpu_init_stack_canary(int cpu, struct task_struct *idle)
{ }

#endif	/* _ASM_STACKPROTECTOR_H */
