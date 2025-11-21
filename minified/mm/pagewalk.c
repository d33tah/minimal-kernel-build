/* STUB: Page table walking functions - return error/no-op */

#include <linux/pagewalk.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include <linux/hugetlb.h>
#include <linux/errno.h>

/* Walk page range with mm_walk_ops - stub returns success after no-op */
int walk_page_range(struct mm_struct *mm, unsigned long start,
		    unsigned long end, const struct mm_walk_ops *ops,
		    void *private)
{
	return 0;
}

/* Walk page range without VMA - stub returns success after no-op */
int walk_page_range_novma(struct mm_struct *mm, unsigned long start,
			  unsigned long end, const struct mm_walk_ops *ops,
			  pgd_t *pgd, void *private)
{
	return 0;
}

/* Walk single VMA - stub returns success after no-op */
int walk_page_vma(struct vm_area_struct *vma, const struct mm_walk_ops *ops,
		  void *private)
{
	return 0;
}

/* Walk page cache mapping - stub returns success after no-op */
int walk_page_mapping(struct address_space *mapping, pgoff_t first_index,
		      pgoff_t last_index, const struct mm_walk_ops *ops,
		      void *private)
{
	return 0;
}
