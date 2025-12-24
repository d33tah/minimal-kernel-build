#ifndef __LINUX_KSM_H
#define __LINUX_KSM_H
struct mm_struct;
static inline void ksm_exit(struct mm_struct *mm) { }
#endif  
