/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KHUGEPAGED_H
#define _LINUX_KHUGEPAGED_H

#include <linux/sched/coredump.h> /* MMF_VM_HUGEPAGE */

static inline void khugepaged_fork(struct mm_struct *mm, struct mm_struct *oldmm)
{
}
static inline void khugepaged_exit(struct mm_struct *mm)
{
}
static inline void khugepaged_enter(struct vm_area_struct *vma,
				    unsigned long vm_flags)
{
}
static inline void khugepaged_enter_vma(struct vm_area_struct *vma,
					unsigned long vm_flags)
{
}
static inline void collapse_pte_mapped_thp(struct mm_struct *mm,
					   unsigned long addr)
{
}

static inline void khugepaged_min_free_kbytes_update(void)
{
}

#endif /* _LINUX_KHUGEPAGED_H */
