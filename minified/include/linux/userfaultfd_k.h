/* userfaultfd_k.h - minimal stubs for CONFIG_USERFAULTFD disabled */
#ifndef _LINUX_USERFAULTFD_K_H
#define _LINUX_USERFAULTFD_K_H

static inline bool is_mergeable_vm_userfaultfd_ctx(struct vm_area_struct *vma,
					struct vm_userfaultfd_ctx vm_ctx)
{
	return true;
}

static inline int userfaultfd_unmap_prep(struct vm_area_struct *vma,
					 unsigned long start, unsigned long end,
					 struct list_head *uf)
{
	return 0;
}

static inline void userfaultfd_unmap_complete(struct mm_struct *mm,
					      struct list_head *uf)
{
}

static inline bool uffd_disable_fault_around(struct vm_area_struct *vma)
{
	return false;
}

static inline bool pte_marker_uffd_wp(pte_t pte)
{
	return false;
}

#endif  
