
#include <linux/mm.h>



/*
 * RUNTIME-DEAD ANCHOR-STUB: move_page_tables (and its entire private helper
 * subtree get_old_pud/get_old_pmd/alloc_new_pud/alloc_new_pmd/take_rmap_locks/
 * drop_rmap_locks/move_soft_dirty_pte/move_ptes/get_extent) is never executed
 * on this kernel's only job (boot + print "Hello, World!" + stay alive).
 *
 * It is link-live via fs/exec.c:shift_arg_pages (exec stack relocation), which
 * itself never runs on this boot, so the whole subtree was removed and the root
 * reduced to its success return value. The sole caller checks
 * `length != move_page_tables(...)`; a full successful move returns
 * `len + old_addr - old_end` with old_addr reaching old_end, i.e. exactly len.
 */
unsigned long move_page_tables(struct vm_area_struct *vma, unsigned long old_addr, struct vm_area_struct *new_vma, unsigned long new_addr, unsigned long len, bool need_rmap_locks)
{
	return len;
}
