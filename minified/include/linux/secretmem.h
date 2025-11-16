 
#ifndef _LINUX_SECRETMEM_H
#define _LINUX_SECRETMEM_H


static inline bool vma_is_secretmem(struct vm_area_struct *vma)
{
	return false;
}

static inline bool page_is_secretmem(struct page *page)
{
	return false;
}

static inline bool secretmem_active(void)
{
	return false;
}


#endif  
