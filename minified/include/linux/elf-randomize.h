/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ELF_RANDOMIZE_H
#define _ELF_RANDOMIZE_H

struct mm_struct;

extern unsigned long arch_mmap_rnd(void);
extern unsigned long arch_randomize_brk(struct mm_struct *mm);
# ifdef CONFIG_COMPAT_BRK
#  define compat_brk_randomized
# endif

#endif
