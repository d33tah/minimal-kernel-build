/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PARAVIRT_H
#define _ASM_X86_PARAVIRT_H
/* Various instructions on x86 need to be replaced for
 * para-virtualization: those hooks are defined here. */

# define default_banner x86_init_noop

#ifndef __ASSEMBLY__
static inline void paravirt_arch_dup_mmap(struct mm_struct *oldmm,
					  struct mm_struct *mm)
{
}

static inline void paravirt_arch_exit_mmap(struct mm_struct *mm)
{
}

static inline void paravirt_set_cap(void)
{
}
#endif /* __ASSEMBLY__ */
#endif /* _ASM_X86_PARAVIRT_H */
