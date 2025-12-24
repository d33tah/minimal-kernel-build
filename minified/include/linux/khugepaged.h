#ifndef _LINUX_KHUGEPAGED_H
#define _LINUX_KHUGEPAGED_H
#include <linux/sched/coredump.h>
static inline void khugepaged_exit(struct mm_struct *mm) {}
static inline void khugepaged_enter_vma(struct vm_area_struct *vma, unsigned long vm_flags) {}
static inline void khugepaged_min_free_kbytes_update(void) {}
#endif  
