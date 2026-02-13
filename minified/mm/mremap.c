#include <linux/mm.h>

unsigned long move_page_tables(struct vm_area_struct *vma,
			       unsigned long old_addr,
			       struct vm_area_struct *new_vma,
			       unsigned long new_addr, unsigned long len,
			       bool need_rmap_locks)
{
	return 0;
}
