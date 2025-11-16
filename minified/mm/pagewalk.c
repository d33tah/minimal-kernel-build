 
#include <linux/pagewalk.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include <linux/hugetlb.h>

 
static int real_depth(int depth)
{
	if (depth == 3 && PTRS_PER_PMD == 1)
		depth = 2;
	if (depth == 2 && PTRS_PER_PUD == 1)
		depth = 1;
	if (depth == 1 && PTRS_PER_P4D == 1)
		depth = 0;
	return depth;
}

static int walk_pte_range_inner(pte_t *pte, unsigned long addr,
				unsigned long end, struct mm_walk *walk)
{
	const struct mm_walk_ops *ops = walk->ops;
	int err = 0;

	for (;;) {
		err = ops->pte_entry(pte, addr, addr + PAGE_SIZE, walk);
		if (err)
		       break;
		if (addr >= end - PAGE_SIZE)
			break;
		addr += PAGE_SIZE;
		pte++;
	}
	return err;
}

static int walk_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pte_t *pte;
	int err = 0;
	spinlock_t *ptl;

	if (walk->no_vma) {
		pte = pte_offset_map(pmd, addr);
		err = walk_pte_range_inner(pte, addr, end, walk);
		pte_unmap(pte);
	} else {
		pte = pte_offset_map_lock(walk->mm, pmd, addr, &ptl);
		err = walk_pte_range_inner(pte, addr, end, walk);
		pte_unmap_unlock(pte, ptl);
	}

	return err;
}

static int walk_hugepd_range(hugepd_t *phpd, unsigned long addr,
			     unsigned long end, struct mm_walk *walk, int pdshift)
{
	return 0;
}

static int walk_pmd_range(pud_t *pud, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pmd_t *pmd;
	unsigned long next;
	const struct mm_walk_ops *ops = walk->ops;
	int err = 0;
	int depth = real_depth(3);

	pmd = pmd_offset(pud, addr);
	do {
again:
		next = pmd_addr_end(addr, end);
		if (pmd_none(*pmd) || (!walk->vma && !walk->no_vma)) {
			if (ops->pte_hole)
				err = ops->pte_hole(addr, next, depth, walk);
			if (err)
				break;
			continue;
		}

		walk->action = ACTION_SUBTREE;

		 
		if (ops->pmd_entry)
			err = ops->pmd_entry(pmd, addr, next, walk);
		if (err)
			break;

		if (walk->action == ACTION_AGAIN)
			goto again;

		 
		if ((!walk->vma && (pmd_leaf(*pmd) || !pmd_present(*pmd))) ||
		    walk->action == ACTION_CONTINUE ||
		    !(ops->pte_entry))
			continue;

		if (walk->vma) {
			split_huge_pmd(walk->vma, pmd, addr);
			if (pmd_trans_unstable(pmd))
				goto again;
		}

		if (is_hugepd(__hugepd(pmd_val(*pmd))))
			err = walk_hugepd_range((hugepd_t *)pmd, addr, next, walk, PMD_SHIFT);
		else
			err = walk_pte_range(pmd, addr, next, walk);
		if (err)
			break;
	} while (pmd++, addr = next, addr != end);

	return err;
}

static int walk_pud_range(p4d_t *p4d, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pud_t *pud;
	unsigned long next;
	const struct mm_walk_ops *ops = walk->ops;
	int err = 0;
	int depth = real_depth(2);

	pud = pud_offset(p4d, addr);
	do {
 again:
		next = pud_addr_end(addr, end);
		if (pud_none(*pud) || (!walk->vma && !walk->no_vma)) {
			if (ops->pte_hole)
				err = ops->pte_hole(addr, next, depth, walk);
			if (err)
				break;
			continue;
		}

		walk->action = ACTION_SUBTREE;

		if (ops->pud_entry)
			err = ops->pud_entry(pud, addr, next, walk);
		if (err)
			break;

		if (walk->action == ACTION_AGAIN)
			goto again;

		if ((!walk->vma && (pud_leaf(*pud) || !pud_present(*pud))) ||
		    walk->action == ACTION_CONTINUE ||
		    !(ops->pmd_entry || ops->pte_entry))
			continue;

		if (walk->vma)
			split_huge_pud(walk->vma, pud, addr);
		if (pud_none(*pud))
			goto again;

		if (is_hugepd(__hugepd(pud_val(*pud))))
			err = walk_hugepd_range((hugepd_t *)pud, addr, next, walk, PUD_SHIFT);
		else
			err = walk_pmd_range(pud, addr, next, walk);
		if (err)
			break;
	} while (pud++, addr = next, addr != end);

	return err;
}

static int walk_p4d_range(pgd_t *pgd, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	p4d_t *p4d;
	unsigned long next;
	const struct mm_walk_ops *ops = walk->ops;
	int err = 0;
	int depth = real_depth(1);

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none_or_clear_bad(p4d)) {
			if (ops->pte_hole)
				err = ops->pte_hole(addr, next, depth, walk);
			if (err)
				break;
			continue;
		}
		if (ops->p4d_entry) {
			err = ops->p4d_entry(p4d, addr, next, walk);
			if (err)
				break;
		}
		if (is_hugepd(__hugepd(p4d_val(*p4d))))
			err = walk_hugepd_range((hugepd_t *)p4d, addr, next, walk, P4D_SHIFT);
		else if (ops->pud_entry || ops->pmd_entry || ops->pte_entry)
			err = walk_pud_range(p4d, addr, next, walk);
		if (err)
			break;
	} while (p4d++, addr = next, addr != end);

	return err;
}

static int walk_pgd_range(unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pgd_t *pgd;
	unsigned long next;
	const struct mm_walk_ops *ops = walk->ops;
	int err = 0;

	if (walk->pgd)
		pgd = walk->pgd + pgd_index(addr);
	else
		pgd = pgd_offset(walk->mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd)) {
			if (ops->pte_hole)
				err = ops->pte_hole(addr, next, 0, walk);
			if (err)
				break;
			continue;
		}
		if (ops->pgd_entry) {
			err = ops->pgd_entry(pgd, addr, next, walk);
			if (err)
				break;
		}
		if (is_hugepd(__hugepd(pgd_val(*pgd))))
			err = walk_hugepd_range((hugepd_t *)pgd, addr, next, walk, PGDIR_SHIFT);
		else if (ops->p4d_entry || ops->pud_entry || ops->pmd_entry || ops->pte_entry)
			err = walk_p4d_range(pgd, addr, next, walk);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);

	return err;
}

static int walk_hugetlb_range(unsigned long addr, unsigned long end,
			      struct mm_walk *walk)
{
	return 0;
}


 
static int walk_page_test(unsigned long start, unsigned long end,
			struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;
	const struct mm_walk_ops *ops = walk->ops;

	if (ops->test_walk)
		return ops->test_walk(start, end, walk);

	 
	if (vma->vm_flags & VM_PFNMAP) {
		int err = 1;
		if (ops->pte_hole)
			err = ops->pte_hole(start, end, -1, walk);
		return err ? err : 1;
	}
	return 0;
}

static int __walk_page_range(unsigned long start, unsigned long end,
			struct mm_walk *walk)
{
	int err = 0;
	struct vm_area_struct *vma = walk->vma;
	const struct mm_walk_ops *ops = walk->ops;

	if (vma && ops->pre_vma) {
		err = ops->pre_vma(start, end, walk);
		if (err)
			return err;
	}

	if (vma && is_vm_hugetlb_page(vma)) {
		if (ops->hugetlb_entry)
			err = walk_hugetlb_range(start, end, walk);
	} else
		err = walk_pgd_range(start, end, walk);

	if (vma && ops->post_vma)
		ops->post_vma(walk);

	return err;
}

 
int walk_page_range(struct mm_struct *mm, unsigned long start,
		unsigned long end, const struct mm_walk_ops *ops,
		void *private)
{
	int err = 0;
	unsigned long next;
	struct vm_area_struct *vma;
	struct mm_walk walk = {
		.ops		= ops,
		.mm		= mm,
		.private	= private,
	};

	if (start >= end)
		return -EINVAL;

	if (!walk.mm)
		return -EINVAL;

	mmap_assert_locked(walk.mm);

	vma = find_vma(walk.mm, start);
	do {
		if (!vma) {  
			walk.vma = NULL;
			next = end;
		} else if (start < vma->vm_start) {  
			walk.vma = NULL;
			next = min(end, vma->vm_start);
		} else {  
			walk.vma = vma;
			next = min(end, vma->vm_end);
			vma = vma->vm_next;

			err = walk_page_test(start, next, &walk);
			if (err > 0) {
				 
				err = 0;
				continue;
			}
			if (err < 0)
				break;
		}
		if (walk.vma || walk.ops->pte_hole)
			err = __walk_page_range(start, next, &walk);
		if (err)
			break;
	} while (start = next, start < end);
	return err;
}

 
int walk_page_range_novma(struct mm_struct *mm, unsigned long start,
			  unsigned long end, const struct mm_walk_ops *ops,
			  pgd_t *pgd,
			  void *private)
{
	struct mm_walk walk = {
		.ops		= ops,
		.mm		= mm,
		.pgd		= pgd,
		.private	= private,
		.no_vma		= true
	};

	if (start >= end || !walk.mm)
		return -EINVAL;

	mmap_assert_locked(walk.mm);

	return __walk_page_range(start, end, &walk);
}

int walk_page_vma(struct vm_area_struct *vma, const struct mm_walk_ops *ops,
		void *private)
{
	struct mm_walk walk = {
		.ops		= ops,
		.mm		= vma->vm_mm,
		.vma		= vma,
		.private	= private,
	};
	int err;

	if (!walk.mm)
		return -EINVAL;

	mmap_assert_locked(walk.mm);

	err = walk_page_test(vma->vm_start, vma->vm_end, &walk);
	if (err > 0)
		return 0;
	if (err < 0)
		return err;
	return __walk_page_range(vma->vm_start, vma->vm_end, &walk);
}

 
int walk_page_mapping(struct address_space *mapping, pgoff_t first_index,
		      pgoff_t nr, const struct mm_walk_ops *ops,
		      void *private)
{
	struct mm_walk walk = {
		.ops		= ops,
		.private	= private,
	};
	struct vm_area_struct *vma;
	pgoff_t vba, vea, cba, cea;
	unsigned long start_addr, end_addr;
	int err = 0;

	lockdep_assert_held(&mapping->i_mmap_rwsem);
	vma_interval_tree_foreach(vma, &mapping->i_mmap, first_index,
				  first_index + nr - 1) {
		 
		vba = vma->vm_pgoff;
		vea = vba + vma_pages(vma);
		cba = first_index;
		cba = max(cba, vba);
		cea = first_index + nr;
		cea = min(cea, vea);

		start_addr = ((cba - vba) << PAGE_SHIFT) + vma->vm_start;
		end_addr = ((cea - vba) << PAGE_SHIFT) + vma->vm_start;
		if (start_addr >= end_addr)
			continue;

		walk.vma = vma;
		walk.mm = vma->vm_mm;

		err = walk_page_test(vma->vm_start, vma->vm_end, &walk);
		if (err > 0) {
			err = 0;
			break;
		} else if (err < 0)
			break;

		err = __walk_page_range(start_addr, end_addr, &walk);
		if (err)
			break;
	}

	return err;
}
