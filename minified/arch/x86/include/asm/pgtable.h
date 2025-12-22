 
#ifndef _ASM_X86_PGTABLE_H
#define _ASM_X86_PGTABLE_H

#include <linux/mem_encrypt.h>
#include <asm/page.h>
#include <asm/pgtable_types.h>

 
#define pgprot_noncached(prot)						\
	((boot_cpu_data.x86 > 3)					\
	 ? (__pgprot(pgprot_val(prot) |					\
		     cachemode2protval(_PAGE_CACHE_MODE_UC_MINUS)))	\
	 : (prot))

#ifndef __ASSEMBLY__
#include <linux/spinlock.h>
#include <asm/x86_init.h>
#include <asm/fpu/api.h>

/* Inlined from asm/pkru.h */
#include <asm/cpufeature.h>
#define PKRU_AD_BIT 0x1u
#define PKRU_WD_BIT 0x2u
#define PKRU_BITS_PER_PKEY 2
#define init_pkru_value	0
#define pkru_get_init_value()	0
static inline bool __pkru_allows_read(u32 pkru, u16 pkey) { int pkru_pkey_bits = pkey * PKRU_BITS_PER_PKEY; return !(pkru & (PKRU_AD_BIT << pkru_pkey_bits)); }
static inline bool __pkru_allows_write(u32 pkru, u16 pkey) { int pkru_pkey_bits = pkey * PKRU_BITS_PER_PKEY; return !(pkru & ((PKRU_AD_BIT|PKRU_WD_BIT) << pkru_pkey_bits)); }
static inline u32 read_pkru(void) { if (cpu_feature_enabled(X86_FEATURE_OSPKE)) return rdpkru(); return 0; }
static inline void pkru_write_default(void) { if (!cpu_feature_enabled(X86_FEATURE_OSPKE)) return; wrpkru(pkru_get_init_value()); }
/* End of pkru.h */
/* --- 2025-12-07 20:42 --- Inlined coco.h */
#include <asm/types.h>
enum cc_vendor {
	CC_VENDOR_NONE,
	CC_VENDOR_AMD,
	CC_VENDOR_HYPERV,
	CC_VENDOR_INTEL,
};
void cc_set_vendor(enum cc_vendor v);
void cc_set_mask(u64 mask);
static inline u64 cc_mkenc(u64 val)
{
	return val;
}
static inline u64 cc_mkdec(u64 val)
{
	return val;
}
#include <linux/page_table_check.h>

/* uffd stubs - only keep functions used in .c files and headers */
static __always_inline pte_t pte_mkuffd_wp(pte_t pte) { return pte; }
static __always_inline int pte_swp_uffd_wp(pte_t pte) { return 0; }
static __always_inline pte_t pte_swp_clear_uffd_wp(pte_t pte) { return pte; }
/* pte_uffd_wp, pmd_uffd_wp, pmd_mkuffd_wp, pte_clear_uffd_wp, pmd_clear_uffd_wp,
   pte_swp_mkuffd_wp, pmd_swp_* removed - never called */

extern pgd_t early_top_pgt[PTRS_PER_PGD];
bool __init __early_make_pgtable(unsigned long address, pmdval_t pmd);

void ptdump_walk_pgd_level(struct seq_file *m, struct mm_struct *mm);
void ptdump_walk_pgd_level_debugfs(struct seq_file *m, struct mm_struct *mm,
				   bool user);
void ptdump_walk_pgd_level_checkwx(void);
void ptdump_walk_user_pgd_level_checkwx(void);

 
#define pgprot_encrypted(prot)	__pgprot(cc_mkenc(pgprot_val(prot)))
#define pgprot_decrypted(prot)	__pgprot(cc_mkdec(pgprot_val(prot)))

#define debug_checkwx()		do { } while (0)
#define debug_checkwx_user()	do { } while (0)

 
extern unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)]
	__visible;
#define ZERO_PAGE(vaddr) ((void)(vaddr),virt_to_page(empty_zero_page))

extern spinlock_t pgd_lock;
extern struct list_head pgd_list;

extern struct mm_struct *pgd_page_get_mm(struct page *page);

extern pmdval_t early_pmd_flags;

#define set_pte(ptep, pte)		native_set_pte(ptep, pte)

#define set_pte_atomic(ptep, pte)					\
	native_set_pte_atomic(ptep, pte)

#define set_pmd(pmdp, pmd)		native_set_pmd(pmdp, pmd)

#ifndef __PAGETABLE_P4D_FOLDED
#define set_pgd(pgdp, pgd)		native_set_pgd(pgdp, pgd)
#define pgd_clear(pgd)			(pgtable_l5_enabled() ? native_pgd_clear(pgd) : 0)
#endif

#ifndef set_p4d
# define set_p4d(p4dp, p4d)		native_set_p4d(p4dp, p4d)
#endif

#ifndef __PAGETABLE_PUD_FOLDED
#define p4d_clear(p4d)			native_p4d_clear(p4d)
#endif

#ifndef set_pud
# define set_pud(pudp, pud)		native_set_pud(pudp, pud)
#endif

#ifndef __PAGETABLE_PUD_FOLDED
#define pud_clear(pud)			native_pud_clear(pud)
#endif

#define pte_clear(mm, addr, ptep)	native_pte_clear(mm, addr, ptep)
#define pmd_clear(pmd)			native_pmd_clear(pmd)

#define pgd_val(x)	native_pgd_val(x)
#define __pgd(x)	native_make_pgd(x)

#ifndef __PAGETABLE_P4D_FOLDED
#define p4d_val(x)	native_p4d_val(x)
#define __p4d(x)	native_make_p4d(x)
#endif

#ifndef __PAGETABLE_PUD_FOLDED
#define pud_val(x)	native_pud_val(x)
#define __pud(x)	native_make_pud(x)
#endif

#ifndef __PAGETABLE_PMD_FOLDED
#define pmd_val(x)	native_pmd_val(x)
#define __pmd(x)	native_make_pmd(x)
#endif

#define pte_val(x)	native_pte_val(x)
#define __pte(x)	native_make_pte(x)

#define arch_end_context_switch(prev)	do {} while(0)

 
static inline int pte_dirty(pte_t pte)
{
	return pte_flags(pte) & _PAGE_DIRTY;
}

static inline int pte_young(pte_t pte)
{
	return pte_flags(pte) & _PAGE_ACCESSED;
}

/* pmd_dirty, pmd_young, pud_dirty, pud_young removed - unused */

static inline int pte_write(pte_t pte)
{
	return pte_flags(pte) & _PAGE_RW;
}

/* pte_huge, pte_global removed - unused */

static inline int pte_exec(pte_t pte)
{
	return !(pte_flags(pte) & _PAGE_NX);
}

static inline int pte_special(pte_t pte)
{
	return pte_flags(pte) & _PAGE_SPECIAL;
}

 

static inline u64 protnone_mask(u64 val);

static inline unsigned long pte_pfn(pte_t pte)
{
	phys_addr_t pfn = pte_val(pte);
	pfn ^= protnone_mask(pfn);
	return (pfn & PTE_PFN_MASK) >> PAGE_SHIFT;
}

static inline unsigned long pmd_pfn(pmd_t pmd)
{
	phys_addr_t pfn = pmd_val(pmd);
	pfn ^= protnone_mask(pfn);
	return (pfn & pmd_pfn_mask(pmd)) >> PAGE_SHIFT;
}

static inline unsigned long pud_pfn(pud_t pud)
{
	phys_addr_t pfn = pud_val(pud);
	pfn ^= protnone_mask(pfn);
	return (pfn & pud_pfn_mask(pud)) >> PAGE_SHIFT;
}

/* p4d_pfn removed - unused */

static inline unsigned long pgd_pfn(pgd_t pgd)
{
	return (pgd_val(pgd) & PTE_PFN_MASK) >> PAGE_SHIFT;
}

#define p4d_leaf	p4d_large
static inline int p4d_large(p4d_t p4d)
{
	 
	return 0;
}

#define pte_page(pte)	pfn_to_page(pte_pfn(pte))

#define pmd_leaf	pmd_large
static inline int pmd_large(pmd_t pte)
{
	return pmd_flags(pte) & _PAGE_PSE;
}


static inline pte_t pte_set_flags(pte_t pte, pteval_t set)
{
	pteval_t v = native_pte_val(pte);

	return native_make_pte(v | set);
}

static inline pte_t pte_clear_flags(pte_t pte, pteval_t clear)
{
	pteval_t v = native_pte_val(pte);

	return native_make_pte(v & ~clear);
}


/* pte_mkclean removed - unused */

static inline pte_t pte_mkold(pte_t pte)
{
	return pte_clear_flags(pte, _PAGE_ACCESSED);
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	return pte_clear_flags(pte, _PAGE_RW);
}

/* pte_mkexec removed - unused */

static inline pte_t pte_mkdirty(pte_t pte)
{
	return pte_set_flags(pte, _PAGE_DIRTY | _PAGE_SOFT_DIRTY);
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	return pte_set_flags(pte, _PAGE_ACCESSED);
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	return pte_set_flags(pte, _PAGE_RW);
}

/* pte_mkhuge, pte_clrhuge, pte_mkglobal, pte_clrglobal removed - unused */

static inline pte_t pte_mkspecial(pte_t pte)
{
	return pte_set_flags(pte, _PAGE_SPECIAL);
}

static inline pte_t pte_mkdevmap(pte_t pte)
{
	return pte_set_flags(pte, _PAGE_SPECIAL|_PAGE_DEVMAP);
}

static inline pmd_t pmd_set_flags(pmd_t pmd, pmdval_t set)
{
	pmdval_t v = native_pmd_val(pmd);

	return native_make_pmd(v | set);
}

/* pmd_mkold, pmd_mkclean, pmd_wrprotect, pmd_mkdirty, pmd_mkhuge, pmd_mkyoung, pmd_mkwrite removed - unused */

static inline pmd_t pmd_mkdevmap(pmd_t pmd)
{
	return pmd_set_flags(pmd, _PAGE_DEVMAP);
}

/* pud_set_flags, pud_clear_flags removed - unused */
/* pud_mkold, pud_mkclean, pud_wrprotect, pud_mkdirty, pud_mkdevmap, pud_mkhuge, pud_mkyoung, pud_mkwrite removed - unused */


 
static inline pgprotval_t massage_pgprot(pgprot_t pgprot)
{
	pgprotval_t protval = pgprot_val(pgprot);

	if (protval & _PAGE_PRESENT)
		protval &= __supported_pte_mask;

	return protval;
}

static inline pgprotval_t check_pgprot(pgprot_t pgprot)
{
	pgprotval_t massaged_val = massage_pgprot(pgprot);

	 

	return massaged_val;
}

static inline pte_t pfn_pte(unsigned long page_nr, pgprot_t pgprot)
{
	phys_addr_t pfn = (phys_addr_t)page_nr << PAGE_SHIFT;
	pfn ^= protnone_mask(pgprot_val(pgprot));
	pfn &= PTE_PFN_MASK;
	return __pte(pfn | check_pgprot(pgprot));
}

static inline pmd_t pfn_pmd(unsigned long page_nr, pgprot_t pgprot)
{
	phys_addr_t pfn = (phys_addr_t)page_nr << PAGE_SHIFT;
	pfn ^= protnone_mask(pgprot_val(pgprot));
	pfn &= PHYSICAL_PMD_PAGE_MASK;
	return __pmd(pfn | check_pgprot(pgprot));
}

/* pfn_pud, pmd_mkinvalid, flip_protnone_guard, pte_modify, pmd_modify removed - unused */

 
#define pgprot_modify pgprot_modify
static inline pgprot_t pgprot_modify(pgprot_t oldprot, pgprot_t newprot)
{
	pgprotval_t preservebits = pgprot_val(oldprot) & _PAGE_CHG_MASK;
	pgprotval_t addbits = pgprot_val(newprot) & ~_PAGE_CHG_MASK;
	return __pgprot(preservebits | addbits);
}

#define pte_pgprot(x) __pgprot(pte_flags(x))
#define pmd_pgprot(x) __pgprot(pmd_flags(x))
#define pud_pgprot(x) __pgprot(pud_flags(x))
#define p4d_pgprot(x) __pgprot(p4d_flags(x))

#define canon_pgprot(p) __pgprot(massage_pgprot(p))

static inline int is_new_memtype_allowed(u64 paddr, unsigned long size,
					 enum page_cache_mode pcm,
					 enum page_cache_mode new_pcm)
{
	 
	if (x86_platform.is_untracked_pat_range(paddr, paddr + size))
		return 1;

	 
	if ((pcm == _PAGE_CACHE_MODE_UC_MINUS &&
	     new_pcm == _PAGE_CACHE_MODE_WB) ||
	    (pcm == _PAGE_CACHE_MODE_WC &&
	     new_pcm == _PAGE_CACHE_MODE_WB) ||
	    (pcm == _PAGE_CACHE_MODE_WT &&
	     new_pcm == _PAGE_CACHE_MODE_WB) ||
	    (pcm == _PAGE_CACHE_MODE_WT &&
	     new_pcm == _PAGE_CACHE_MODE_WC)) {
		return 0;
	}

	return 1;
}

pmd_t *populate_extra_pmd(unsigned long vaddr);
pte_t *populate_extra_pte(unsigned long vaddr);

/* pti_set_user_pgtbl removed - unused */

#endif	 


# include <asm/pgtable_32.h>

#ifndef __ASSEMBLY__
#include <linux/mm_types.h>
#include <linux/mmdebug.h>
#include <linux/log2.h>
#include <asm/fixmap.h>

static inline int pte_none(pte_t pte)
{
	return !(pte.pte & ~(_PAGE_KNL_ERRATUM_MASK));
}

#define __HAVE_ARCH_PTE_SAME
static inline int pte_same(pte_t a, pte_t b)
{
	return a.pte == b.pte;
}

static inline int pte_present(pte_t a)
{
	return pte_flags(a) & (_PAGE_PRESENT | _PAGE_PROTNONE);
}


#define pte_accessible pte_accessible
static inline bool pte_accessible(struct mm_struct *mm, pte_t a)
{
	if (pte_flags(a) & _PAGE_PRESENT)
		return true;

	if ((pte_flags(a) & _PAGE_PROTNONE) &&
			atomic_read(&mm->tlb_flush_pending))
		return true;

	return false;
}

static inline int pmd_present(pmd_t pmd)
{
	 
	return pmd_flags(pmd) & (_PAGE_PRESENT | _PAGE_PROTNONE | _PAGE_PSE);
}


static inline int pmd_none(pmd_t pmd)
{
	 
	unsigned long val = native_pmd_val(pmd);
	return (val & ~_PAGE_KNL_ERRATUM_MASK) == 0;
}

static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	return (unsigned long)__va(pmd_val(pmd) & pmd_pfn_mask(pmd));
}

 
#define pmd_page(pmd)	pfn_to_page(pmd_pfn(pmd))

 
#define mk_pte(page, pgprot)   pfn_pte(page_to_pfn(page), (pgprot))

static inline int pmd_bad(pmd_t pmd)
{
	return (pmd_flags(pmd) & ~_PAGE_USER) != _KERNPG_TABLE;
}

static inline unsigned long pages_to_mb(unsigned long npg)
{
	return npg >> (20 - PAGE_SHIFT);
}

/* CONFIG_PGTABLE_LEVELS == 2, so only 2-level paging supported */
#define pud_leaf	pud_large
static inline int pud_large(pud_t pud)
{
	return 0;
}

#endif 

#define KERNEL_PGD_BOUNDARY	pgd_index(PAGE_OFFSET)
#define KERNEL_PGD_PTRS		(PTRS_PER_PGD - KERNEL_PGD_BOUNDARY)

#ifndef __ASSEMBLY__

extern int direct_gbpages;
void init_mem_mapping(void);
void early_alloc_pgt_buf(void);
void __init poking_init(void);
unsigned long init_memory_mapping(unsigned long start,
				  unsigned long end, pgprot_t prot);


 
static inline pte_t native_local_ptep_get_and_clear(pte_t *ptep)
{
	pte_t res = *ptep;

	 
	native_pte_clear(NULL, 0, ptep);
	return res;
}

static inline pmd_t native_local_pmdp_get_and_clear(pmd_t *pmdp)
{
	pmd_t res = *pmdp;

	native_pmd_clear(pmdp);
	return res;
}

static inline pud_t native_local_pudp_get_and_clear(pud_t *pudp)
{
	pud_t res = *pudp;

	native_pud_clear(pudp);
	return res;
}

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pte)
{
	page_table_check_pte_set(mm, addr, ptep, pte);
	set_pte(ptep, pte);
}

/* set_pmd_at, set_pud_at removed - unused */


struct vm_area_struct;

#define  __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
extern int ptep_set_access_flags(struct vm_area_struct *vma,
				 unsigned long address, pte_t *ptep,
				 pte_t entry, int dirty);

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
extern int ptep_test_and_clear_young(struct vm_area_struct *vma,
				     unsigned long addr, pte_t *ptep);

#define __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
extern int ptep_clear_flush_young(struct vm_area_struct *vma,
				  unsigned long address, pte_t *ptep);

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
				       pte_t *ptep)
{
	pte_t pte = native_ptep_get_and_clear(ptep);
	page_table_check_pte_clear(mm, addr, pte);
	return pte;
}

#define flush_tlb_fix_spurious_fault(vma, address) do { } while (0)

#define mk_pmd(page, pgprot)   pfn_pmd(page_to_pfn(page), (pgprot))

/* pmdp_set_access_flags, pudp_set_access_flags, pmdp_test_and_clear_young,
   pudp_test_and_clear_young, pmdp_clear_flush_young removed - unused */


/* pmd_write, pmdp_huge_get_and_clear, pudp_huge_get_and_clear, pmdp_set_wrprotect removed - unused */

#define pud_write pud_write
static inline int pud_write(pud_t pud)
{
	return pud_flags(pud) & _PAGE_RW;
}

#ifndef pmdp_establish
#define pmdp_establish pmdp_establish
static inline pmd_t pmdp_establish(struct vm_area_struct *vma,
		unsigned long address, pmd_t *pmdp, pmd_t pmd)
{
	pmd_t old;
	page_table_check_pmd_set(vma->vm_mm, address, pmdp, pmd);
	old = *pmdp;
	WRITE_ONCE(*pmdp, pmd);
	return old;
}
#endif

/* pmdp_invalidate_ad, pgdp_maps_userspace, pgd_large removed - unused */


static inline void clone_pgd_range(pgd_t *dst, pgd_t *src, int count)
{
	memcpy(dst, src, count * sizeof(pgd_t));
}

/* page_level_shift, page_level_size, page_level_mask removed - unused */

 
static inline void update_mmu_cache(struct vm_area_struct *vma,
		unsigned long addr, pte_t *ptep)
{
}
/* update_mmu_cache_pmd, update_mmu_cache_pud removed - unused */
/* pte_swp_exclusive, pte_swp_clear_exclusive removed - unused (entire _PAGE_SWP_EXCLUSIVE block) */



static inline u16 pte_flags_pkey(unsigned long pte_flags)
{
	return 0;
}

static inline bool __pkru_allows_pkey(u16 pkey, bool write)
{
	u32 pkru = read_pkru();

	if (!__pkru_allows_read(pkru, pkey))
		return false;
	if (write && !__pkru_allows_write(pkru, pkey))
		return false;

	return true;
}

 
static inline bool __pte_access_permitted(unsigned long pteval, bool write)
{
	unsigned long need_pte_bits = _PAGE_PRESENT|_PAGE_USER;

	if (write)
		need_pte_bits |= _PAGE_RW;

	if ((pteval & need_pte_bits) != need_pte_bits)
		return 0;

	return __pkru_allows_pkey(pte_flags_pkey(pteval), write);
}

#define pte_access_permitted pte_access_permitted
static inline bool pte_access_permitted(pte_t pte, bool write)
{
	return __pte_access_permitted(pte_val(pte), write);
}

/* pmd_access_permitted, pud_access_permitted removed - unused */

#define __HAVE_ARCH_PFN_MODIFY_ALLOWED 1
extern bool pfn_modify_allowed(unsigned long pfn, pgprot_t prot);

/* arch_has_pfn_modify_check, arch_faults_on_old_pte removed - unused */

#endif

#endif  
