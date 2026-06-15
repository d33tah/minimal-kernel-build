/* userfaultfd_k.h - minimal stubs for CONFIG_USERFAULTFD disabled */
#ifndef _LINUX_USERFAULTFD_K_H
#define _LINUX_USERFAULTFD_K_H

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

#endif
