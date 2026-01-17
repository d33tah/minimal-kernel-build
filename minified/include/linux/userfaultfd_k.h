/* userfaultfd_k.h - minimal stubs for CONFIG_USERFAULTFD disabled */
#ifndef _LINUX_USERFAULTFD_K_H
#define _LINUX_USERFAULTFD_K_H
/* is_mergeable_vm_userfaultfd_ctx, userfaultfd_unmap_prep, uffd_disable_fault_around, pte_marker_uffd_wp removed - no callers */
static inline void userfaultfd_unmap_complete(struct mm_struct *mm,
					      struct list_head *uf) { }
#endif  
