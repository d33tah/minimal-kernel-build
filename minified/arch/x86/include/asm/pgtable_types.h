 
#ifndef _ASM_X86_PGTABLE_DEFS_H
#define _ASM_X86_PGTABLE_DEFS_H

#include <linux/const.h>
#include <linux/mem_encrypt.h>

#include <asm/page_types.h>

#define _PAGE_BIT_PRESENT	0	 
#define _PAGE_BIT_RW		1	 
#define _PAGE_BIT_USER		2	 
#define _PAGE_BIT_PWT		3	 
#define _PAGE_BIT_PCD		4	 
#define _PAGE_BIT_ACCESSED	5	 
#define _PAGE_BIT_DIRTY		6	 
#define _PAGE_BIT_PSE		7	 
#define _PAGE_BIT_PAT		7	 
#define _PAGE_BIT_GLOBAL	8	 
#define _PAGE_BIT_SOFTW1	9	 
#define _PAGE_BIT_SOFTW2	10	 
#define _PAGE_BIT_SOFTW3	11	 
#define _PAGE_BIT_PAT_LARGE	12
#define _PAGE_BIT_NX		63
#define _PAGE_BIT_SPECIAL	_PAGE_BIT_SOFTW1
#define _PAGE_BIT_PROTNONE	_PAGE_BIT_GLOBAL

#define _PAGE_PRESENT	(_AT(pteval_t, 1) << _PAGE_BIT_PRESENT)
#define _PAGE_RW	(_AT(pteval_t, 1) << _PAGE_BIT_RW)
#define _PAGE_USER	(_AT(pteval_t, 1) << _PAGE_BIT_USER)
#define _PAGE_PWT	(_AT(pteval_t, 1) << _PAGE_BIT_PWT)
#define _PAGE_PCD	(_AT(pteval_t, 1) << _PAGE_BIT_PCD)
#define _PAGE_ACCESSED	(_AT(pteval_t, 1) << _PAGE_BIT_ACCESSED)
#define _PAGE_DIRTY	(_AT(pteval_t, 1) << _PAGE_BIT_DIRTY)
#define _PAGE_PSE	(_AT(pteval_t, 1) << _PAGE_BIT_PSE)
#define _PAGE_GLOBAL	(_AT(pteval_t, 1) << _PAGE_BIT_GLOBAL)
#define _PAGE_SOFTW1	(_AT(pteval_t, 1) << _PAGE_BIT_SOFTW1)
#define _PAGE_SOFTW2	(_AT(pteval_t, 1) << _PAGE_BIT_SOFTW2)
#define _PAGE_SOFTW3	(_AT(pteval_t, 1) << _PAGE_BIT_SOFTW3)
#define _PAGE_PAT	(_AT(pteval_t, 1) << _PAGE_BIT_PAT)
#define _PAGE_PAT_LARGE (_AT(pteval_t, 1) << _PAGE_BIT_PAT_LARGE)
#define _PAGE_SPECIAL	(_AT(pteval_t, 1) << _PAGE_BIT_SPECIAL)
#define _PAGE_PKEY_BIT0	(_AT(pteval_t, 0))
#define _PAGE_PKEY_BIT1	(_AT(pteval_t, 0))
#define _PAGE_PKEY_BIT2	(_AT(pteval_t, 0))
#define _PAGE_PKEY_BIT3	(_AT(pteval_t, 0))

#define _PAGE_KNL_ERRATUM_MASK 0

#define _PAGE_SOFT_DIRTY	(_AT(pteval_t, 0))

 
#define _PAGE_SWP_SOFT_DIRTY	(_AT(pteval_t, 0))

#define _PAGE_UFFD_WP		(_AT(pteval_t, 0))
#define _PAGE_SWP_UFFD_WP	(_AT(pteval_t, 0))

#define _PAGE_NX	(_AT(pteval_t, 0))
#define _PAGE_DEVMAP	(_AT(pteval_t, 0))
#define _PAGE_SOFTW4	(_AT(pteval_t, 0))

#define _PAGE_PROTNONE	(_AT(pteval_t, 1) << _PAGE_BIT_PROTNONE)

 
#define _PAGE_CHG_MASK	(PTE_PFN_MASK | _PAGE_PCD | _PAGE_PWT |		\
			 _PAGE_SPECIAL | _PAGE_ACCESSED | _PAGE_DIRTY |	\
			 _PAGE_SOFT_DIRTY | _PAGE_DEVMAP | _PAGE_ENC |  \
			 _PAGE_UFFD_WP)
#define _HPAGE_CHG_MASK (_PAGE_CHG_MASK | _PAGE_PSE)

 
#ifndef __ASSEMBLY__
enum page_cache_mode {
	_PAGE_CACHE_MODE_WB       = 0,
	_PAGE_CACHE_MODE_WC       = 1,
	_PAGE_CACHE_MODE_UC_MINUS = 2,
	_PAGE_CACHE_MODE_UC       = 3,
	_PAGE_CACHE_MODE_WT       = 4,
	_PAGE_CACHE_MODE_WP       = 5,

	_PAGE_CACHE_MODE_NUM      = 8
};
#endif

#define _PAGE_ENC		(_AT(pteval_t, sme_me_mask))

#define _PAGE_CACHE_MASK	(_PAGE_PWT | _PAGE_PCD | _PAGE_PAT)
#define _PAGE_LARGE_CACHE_MASK	(_PAGE_PWT | _PAGE_PCD | _PAGE_PAT_LARGE)

#define _PAGE_NOCACHE		(cachemode2protval(_PAGE_CACHE_MODE_UC))
#define _PAGE_CACHE_WP		(cachemode2protval(_PAGE_CACHE_MODE_WP))

#define __PP _PAGE_PRESENT
#define __RW _PAGE_RW
#define _USR _PAGE_USER
#define ___A _PAGE_ACCESSED
#define ___D _PAGE_DIRTY
#define ___G _PAGE_GLOBAL
#define __NX _PAGE_NX

#define _ENC _PAGE_ENC
#define __WP _PAGE_CACHE_WP
#define __NC _PAGE_NOCACHE
#define _PSE _PAGE_PSE

#define pgprot_val(x)		((x).pgprot)
#define __pgprot(x)		((pgprot_t) { (x) } )
#define __pg(x)			__pgprot(x)

#define PAGE_NONE	     __pg(   0|   0|   0|___A|   0|   0|   0|___G)
#define PAGE_SHARED	     __pg(__PP|__RW|_USR|___A|__NX|   0|   0|   0)
#define PAGE_SHARED_EXEC     __pg(__PP|__RW|_USR|___A|   0|   0|   0|   0)
#define PAGE_COPY_NOEXEC     __pg(__PP|   0|_USR|___A|__NX|   0|   0|   0)
#define PAGE_COPY_EXEC	     __pg(__PP|   0|_USR|___A|   0|   0|   0|   0)
#define PAGE_COPY	     __pg(__PP|   0|_USR|___A|__NX|   0|   0|   0)
#define PAGE_READONLY	     __pg(__PP|   0|_USR|___A|__NX|   0|   0|   0)
#define PAGE_READONLY_EXEC   __pg(__PP|   0|_USR|___A|   0|   0|   0|   0)

#define __PAGE_KERNEL		 (__PP|__RW|   0|___A|__NX|___D|   0|___G)
#define __PAGE_KERNEL_EXEC	 (__PP|__RW|   0|___A|   0|___D|   0|___G)
#define _KERNPG_TABLE_NOENC	 (__PP|__RW|   0|___A|   0|___D|   0|   0)
#define _KERNPG_TABLE		 (__PP|__RW|   0|___A|   0|___D|   0|   0| _ENC)
#define _PAGE_TABLE_NOENC	 (__PP|__RW|_USR|___A|   0|___D|   0|   0)
#define _PAGE_TABLE		 (__PP|__RW|_USR|___A|   0|___D|   0|   0| _ENC)
#define __PAGE_KERNEL_RO	 (__PP|   0|   0|___A|__NX|___D|   0|___G)
#define __PAGE_KERNEL_ROX	 (__PP|   0|   0|___A|   0|___D|   0|___G)
#define __PAGE_KERNEL_NOCACHE	 (__PP|__RW|   0|___A|__NX|___D|   0|___G| __NC)
#define __PAGE_KERNEL_VVAR	 (__PP|   0|_USR|___A|__NX|___D|   0|___G)
#define __PAGE_KERNEL_LARGE	 (__PP|__RW|   0|___A|__NX|___D|_PSE|___G)
#define __PAGE_KERNEL_LARGE_EXEC (__PP|__RW|   0|___A|   0|___D|_PSE|___G)
#define __PAGE_KERNEL_WP	 (__PP|__RW|   0|___A|__NX|___D|   0|___G| __WP)


#define __PAGE_KERNEL_IO		__PAGE_KERNEL
#define __PAGE_KERNEL_IO_NOCACHE	__PAGE_KERNEL_NOCACHE


#ifndef __ASSEMBLY__

#define __PAGE_KERNEL_ENC	(__PAGE_KERNEL    | _ENC)
#define __PAGE_KERNEL_ENC_WP	(__PAGE_KERNEL_WP | _ENC)
#define __PAGE_KERNEL_NOENC	(__PAGE_KERNEL    |    0)
#define __PAGE_KERNEL_NOENC_WP	(__PAGE_KERNEL_WP |    0)

#define __pgprot_mask(x)	__pgprot((x) & __default_kernel_pte_mask)

#define PAGE_KERNEL		__pgprot_mask(__PAGE_KERNEL            | _ENC)
#define PAGE_KERNEL_NOENC	__pgprot_mask(__PAGE_KERNEL            |    0)
#define PAGE_KERNEL_RO		__pgprot_mask(__PAGE_KERNEL_RO         | _ENC)
#define PAGE_KERNEL_EXEC	__pgprot_mask(__PAGE_KERNEL_EXEC       | _ENC)
#define PAGE_KERNEL_EXEC_NOENC	__pgprot_mask(__PAGE_KERNEL_EXEC       |    0)
#define PAGE_KERNEL_ROX		__pgprot_mask(__PAGE_KERNEL_ROX        | _ENC)
#define PAGE_KERNEL_NOCACHE	__pgprot_mask(__PAGE_KERNEL_NOCACHE    | _ENC)
#define PAGE_KERNEL_LARGE	__pgprot_mask(__PAGE_KERNEL_LARGE      | _ENC)
#define PAGE_KERNEL_LARGE_EXEC	__pgprot_mask(__PAGE_KERNEL_LARGE_EXEC | _ENC)
#define PAGE_KERNEL_VVAR	__pgprot_mask(__PAGE_KERNEL_VVAR       | _ENC)

#define PAGE_KERNEL_IO		__pgprot_mask(__PAGE_KERNEL_IO)
#define PAGE_KERNEL_IO_NOCACHE	__pgprot_mask(__PAGE_KERNEL_IO_NOCACHE)

#endif	 

 
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY_EXEC
#define __P101	PAGE_READONLY_EXEC
#define __P110	PAGE_COPY_EXEC
#define __P111	PAGE_COPY_EXEC

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY_EXEC
#define __S101	PAGE_READONLY_EXEC
#define __S110	PAGE_SHARED_EXEC
#define __S111	PAGE_SHARED_EXEC

 
#define PTE_IDENT_ATTR	 0x003		 
#define PDE_IDENT_ATTR	 0x063		 
#define PGD_IDENT_ATTR	 0x001		 

# include <asm/pgtable_32_types.h>

#ifndef __ASSEMBLY__

#include <linux/types.h>

 
#define PTE_PFN_MASK		((pteval_t)PHYSICAL_PAGE_MASK)

 
#define PTE_FLAGS_MASK		(~PTE_PFN_MASK)

typedef struct pgprot { pgprotval_t pgprot; } pgprot_t;

typedef struct { pgdval_t pgd; } pgd_t;

static inline pgprot_t pgprot_nx(pgprot_t prot)
{
	return __pgprot(pgprot_val(prot) | _PAGE_NX);
}
#define pgprot_nx pgprot_nx

 
#define PGD_ALLOWED_BITS	(~0ULL)

static inline pgd_t native_make_pgd(pgdval_t val)
{
	return (pgd_t) { val & PGD_ALLOWED_BITS };
}

static inline pgdval_t native_pgd_val(pgd_t pgd)
{
	return pgd.pgd & PGD_ALLOWED_BITS;
}

/* --- 2025-12-07 10:14 --- Inlined asm-generic/pgtable-nop4d.h content */
#define __PAGETABLE_P4D_FOLDED 1

typedef struct { pgd_t pgd; } p4d_t;

#define P4D_SHIFT		PGDIR_SHIFT
#define PTRS_PER_P4D		1
#define P4D_SIZE		(1UL << P4D_SHIFT)
#define P4D_MASK		(~(P4D_SIZE-1))

static inline int pgd_none(pgd_t pgd)		{ return 0; }
static inline int pgd_bad(pgd_t pgd)		{ return 0; }
static inline int pgd_present(pgd_t pgd)	{ return 1; }
static inline void pgd_clear(pgd_t *pgd)	{ }
#define p4d_ERROR(p4d)				(pgd_ERROR((p4d).pgd))

#define pgd_populate(mm, pgd, p4d)		do { } while (0)
#define pgd_populate_safe(mm, pgd, p4d)		do { } while (0)
#define set_pgd(pgdptr, pgdval)	set_p4d((p4d_t *)(pgdptr), (p4d_t) { pgdval })

static inline p4d_t *p4d_offset(pgd_t *pgd, unsigned long address)
{
	return (p4d_t *)pgd;
}

#define p4d_val(x)				(pgd_val((x).pgd))
#define __p4d(x)				((p4d_t) { __pgd(x) })

#define pgd_page(pgd)				(p4d_page((p4d_t){ pgd }))
#define pgd_page_vaddr(pgd)			((unsigned long)(p4d_pgtable((p4d_t){ pgd })))

#define p4d_alloc_one(mm, address)		NULL
#define p4d_free(mm, x)				do { } while (0)
#define p4d_free_tlb(tlb, x, a)			do { } while (0)

#undef  p4d_addr_end
#define p4d_addr_end(addr, end)			(end)

static inline p4dval_t native_p4d_val(p4d_t p4d)
{
	return native_pgd_val(p4d.pgd);
}

/* --- 2025-12-07 10:14 --- Inlined asm-generic/pgtable-nopud.h content */
#define __PAGETABLE_PUD_FOLDED 1

typedef struct { p4d_t p4d; } pud_t;

#define PUD_SHIFT	P4D_SHIFT
#define PTRS_PER_PUD	1
#define PUD_SIZE  	(1UL << PUD_SHIFT)
#define PUD_MASK  	(~(PUD_SIZE-1))

static inline int p4d_none(p4d_t p4d)		{ return 0; }
static inline int p4d_bad(p4d_t p4d)		{ return 0; }
static inline int p4d_present(p4d_t p4d)	{ return 1; }
static inline void p4d_clear(p4d_t *p4d)	{ }
#define pud_ERROR(pud)				(p4d_ERROR((pud).p4d))

#define p4d_populate(mm, p4d, pud)		do { } while (0)
#define p4d_populate_safe(mm, p4d, pud)		do { } while (0)
#define set_p4d(p4dptr, p4dval)	set_pud((pud_t *)(p4dptr), (pud_t) { p4dval })

static inline pud_t *pud_offset(p4d_t *p4d, unsigned long address)
{
	return (pud_t *)p4d;
}
#define pud_offset pud_offset

#define pud_val(x)				(p4d_val((x).p4d))
#define __pud(x)				((pud_t) { __p4d(x) })

#define p4d_page(p4d)				(pud_page((pud_t){ p4d }))
#define p4d_pgtable(p4d)			((pud_t *)(pud_pgtable((pud_t){ p4d })))

#define pud_alloc_one(mm, address)		NULL
#define pud_free(mm, x)				do { } while (0)
#define pud_free_tlb(tlb, x, a)		        do { } while (0)

#undef  pud_addr_end
#define pud_addr_end(addr, end)			(end)

static inline pudval_t native_pud_val(pud_t pud)
{
	return native_pgd_val(pud.p4d.pgd);
}

/* --- 2025-12-07 10:14 --- Inlined asm-generic/pgtable-nopmd.h content */
struct mm_struct;

#define __PAGETABLE_PMD_FOLDED 1

typedef struct { pud_t pud; } pmd_t;

#define PMD_SHIFT	PUD_SHIFT
#define PTRS_PER_PMD	1
#define PMD_SIZE  	(1UL << PMD_SHIFT)
#define PMD_MASK  	(~(PMD_SIZE-1))

static inline int pud_none(pud_t pud)		{ return 0; }
static inline int pud_bad(pud_t pud)		{ return 0; }
static inline int pud_present(pud_t pud)	{ return 1; }
/* pud_user removed - unused */
static inline int pud_leaf(pud_t pud)		{ return 0; }
static inline void pud_clear(pud_t *pud)	{ }
#define pmd_ERROR(pmd)				(pud_ERROR((pmd).pud))

#define pud_populate(mm, pmd, pte)		do { } while (0)

#define set_pud(pudptr, pudval)			set_pmd((pmd_t *)(pudptr), (pmd_t) { pudval })

static inline pmd_t * pmd_offset(pud_t * pud, unsigned long address)
{
	return (pmd_t *)pud;
}
#define pmd_offset pmd_offset

#define pmd_val(x)				(pud_val((x).pud))
#define __pmd(x)				((pmd_t) { __pud(x) } )

#define pud_page(pud)				(pmd_page((pmd_t){ pud }))
#define pud_pgtable(pud)			((pmd_t *)(pmd_page_vaddr((pmd_t){ pud })))

#define pmd_alloc_one(mm, address)		NULL
#define pmd_free_tlb(tlb, x, a)		do { } while (0)

#undef  pmd_addr_end
#define pmd_addr_end(addr, end)			(end)

static inline pmdval_t native_pmd_val(pmd_t pmd)
{
	return native_pgd_val(pmd.pud.p4d.pgd);
}

static inline p4dval_t p4d_pfn_mask(p4d_t p4d)
{
	 
	return PTE_PFN_MASK;
}

static inline p4dval_t p4d_flags_mask(p4d_t p4d)
{
	return ~p4d_pfn_mask(p4d);
}

static inline p4dval_t p4d_flags(p4d_t p4d)
{
	return native_p4d_val(p4d) & p4d_flags_mask(p4d);
}

static inline pudval_t pud_pfn_mask(pud_t pud)
{
	if (native_pud_val(pud) & _PAGE_PSE)
		return PHYSICAL_PUD_PAGE_MASK;
	else
		return PTE_PFN_MASK;
}

static inline pudval_t pud_flags_mask(pud_t pud)
{
	return ~pud_pfn_mask(pud);
}

static inline pudval_t pud_flags(pud_t pud)
{
	return native_pud_val(pud) & pud_flags_mask(pud);
}

static inline pmdval_t pmd_pfn_mask(pmd_t pmd)
{
	if (native_pmd_val(pmd) & _PAGE_PSE)
		return PHYSICAL_PMD_PAGE_MASK;
	else
		return PTE_PFN_MASK;
}

static inline pmdval_t pmd_flags_mask(pmd_t pmd)
{
	return ~pmd_pfn_mask(pmd);
}

static inline pmdval_t pmd_flags(pmd_t pmd)
{
	return native_pmd_val(pmd) & pmd_flags_mask(pmd);
}

static inline pte_t native_make_pte(pteval_t val)
{
	return (pte_t) { .pte = val };
}

static inline pteval_t native_pte_val(pte_t pte)
{
	return pte.pte;
}

static inline pteval_t pte_flags(pte_t pte)
{
	return native_pte_val(pte) & PTE_FLAGS_MASK;
}

#define __pte2cm_idx(cb)				\
	((((cb) >> (_PAGE_BIT_PAT - 2)) & 4) |		\
	 (((cb) >> (_PAGE_BIT_PCD - 1)) & 2) |		\
	 (((cb) >> _PAGE_BIT_PWT) & 1))

unsigned long cachemode2protval(enum page_cache_mode pcm);

/* protval_4k_2_large, pgprot_4k_2_large, protval_large_2_4k, pgprot_large_2_4k removed - unused */


typedef struct page *pgtable_t;

extern pteval_t __supported_pte_mask;
extern pteval_t __default_kernel_pte_mask;
/* set_nx, nx_enabled removed - unused */

#define pgprot_writecombine	pgprot_writecombine
extern pgprot_t pgprot_writecombine(pgprot_t prot);

#define pgprot_writethrough	pgprot_writethrough
extern pgprot_t pgprot_writethrough(pgprot_t prot);

#define __HAVE_PFNMAP_TRACKING

#define __HAVE_PHYS_MEM_ACCESS_PROT
struct file;
pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
                              unsigned long size, pgprot_t vma_prot);

void set_pte_vaddr(unsigned long vaddr, pte_t pte);

extern void native_pagetable_init(void);

/* arch_report_meminfo removed - unused */

enum pg_level {
	PG_LEVEL_NONE,
	PG_LEVEL_4K,
	PG_LEVEL_2M,
	PG_LEVEL_1G,
	PG_LEVEL_512G,
	PG_LEVEL_NUM
};

static inline void update_page_count(int level, unsigned long pages) { }

/* lookup_address, lookup_address_in_pgd, lookup_pmd_address,
   slow_virt_to_phys, kernel_map_pages_in_pgd, kernel_unmap_pages_in_pgd
   removed - unused */
#endif 

#endif  
