#ifndef _LINUX_RMAP_H
#define _LINUX_RMAP_H

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/rwsem.h>
#include <linux/memcontrol.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/memremap.h>

struct anon_vma {
	struct anon_vma *root;		 
	struct rw_semaphore rwsem;	 
	 
	atomic_t refcount;

	 
	unsigned degree;

	struct anon_vma *parent;	 

	 

	 
	struct rb_root_cached rb_root;
};

struct anon_vma_chain {
	struct vm_area_struct *vma;
	struct anon_vma *anon_vma;
	struct list_head same_vma;    
	struct rb_node rb;			 
	unsigned long rb_subtree_last;
};

enum ttu_flags {
	TTU_SPLIT_HUGE_PMD	= 0x4,
	TTU_IGNORE_MLOCK	= 0x8,
	TTU_SYNC		= 0x10,
	TTU_BATCH_FLUSH		= 0x40,
	TTU_RMAP_LOCKED		= 0x80,
};

static inline void get_anon_vma(struct anon_vma *anon_vma)
{
	atomic_inc(&anon_vma->refcount);
}

void __put_anon_vma(struct anon_vma *anon_vma);

static inline void put_anon_vma(struct anon_vma *anon_vma)
{
	if (atomic_dec_and_test(&anon_vma->refcount))
		__put_anon_vma(anon_vma);
}

static inline void anon_vma_lock_write(struct anon_vma *anon_vma)
{
	down_write(&anon_vma->root->rwsem);
}

static inline void anon_vma_unlock_write(struct anon_vma *anon_vma)
{
	up_write(&anon_vma->root->rwsem);
}

void anon_vma_init(void);	 
int  __anon_vma_prepare(struct vm_area_struct *);
void unlink_anon_vmas(struct vm_area_struct *);
int anon_vma_clone(struct vm_area_struct *, struct vm_area_struct *);
int anon_vma_fork(struct vm_area_struct *, struct vm_area_struct *);

static inline int anon_vma_prepare(struct vm_area_struct *vma)
{
	if (likely(vma->anon_vma))
		return 0;

	return __anon_vma_prepare(vma);
}

typedef int __bitwise rmap_t;

#define RMAP_NONE		((__force rmap_t)0)

#define RMAP_EXCLUSIVE		((__force rmap_t)BIT(0))

#define RMAP_COMPOUND		((__force rmap_t)BIT(1))

void page_add_anon_rmap(struct page *, struct vm_area_struct *,
		unsigned long address, rmap_t flags);
void page_add_new_anon_rmap(struct page *, struct vm_area_struct *,
		unsigned long address);
void page_add_file_rmap(struct page *, struct vm_area_struct *,
		bool compound);
void page_remove_rmap(struct page *, struct vm_area_struct *,
		bool compound);

#define PVMW_SYNC		(1 << 0)
#define PVMW_MIGRATION		(1 << 1)

struct page_vma_mapped_walk {
	unsigned long pfn;
	unsigned long nr_pages;
	pgoff_t pgoff;
	struct vm_area_struct *vma;
	unsigned long address;
	pmd_t *pmd;
	pte_t *pte;
	spinlock_t *ptl;
	unsigned int flags;
};

static inline void page_vma_mapped_walk_done(struct page_vma_mapped_walk *pvmw)
{
	 
	if (pvmw->pte && !is_vm_hugetlb_page(pvmw->vma))
		pte_unmap(pvmw->pte);
	if (pvmw->ptl)
		spin_unlock(pvmw->ptl);
}

bool page_vma_mapped_walk(struct page_vma_mapped_walk *pvmw);


int folio_mkclean(struct folio *);

void remove_migration_ptes(struct folio *src, struct folio *dst, bool locked);

int page_mapped_in_vma(struct page *page, struct vm_area_struct *vma);


static inline int page_mkclean(struct page *page)
{
	return folio_mkclean(page_folio(page));
}
#endif	 
