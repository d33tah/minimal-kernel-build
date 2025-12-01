#ifndef _LINUX_HUGETLB_INLINE_H
#define _LINUX_HUGETLB_INLINE_H


static inline bool is_vm_hugetlb_page(struct vm_area_struct *vma)
{
	return false;
}


#endif
