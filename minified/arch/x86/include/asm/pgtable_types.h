 
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
#define _PAGE_BIT_GLOBAL	8	 
#define _PAGE_BIT_SOFTW1	9
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
#define _PAGE_SPECIAL	(_AT(pteval_t, 1) << _PAGE_BIT_SPECIAL)

#define _PAGE_KNL_ERRATUM_MASK 0

#define _PAGE_SOFT_DIRTY	(_AT(pteval_t, 0))

#define _PAGE_NX	(_AT(pteval_t, 0))

#define _PAGE_PROTNONE	(_AT(pteval_t, 1) << _PAGE_BIT_PROTNONE)

#ifndef __ASSEMBLY__
struct mm_struct;
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

#define __PP _PAGE_PRESENT
#define __RW _PAGE_RW
#define _USR _PAGE_USER
#define ___A _PAGE_ACCESSED
#define ___D _PAGE_DIRTY
#define ___G _PAGE_GLOBAL
#define __NX _PAGE_NX

#define _ENC _PAGE_ENC
#define _PSE _PAGE_PSE

#define pgprot_val(x)		((x).pgprot)
#define __pgprot(x)		((pgprot_t) { (x) } )
#define __pg(x)			__pgprot(x)

#define PAGE_NONE	     __pg(   0|   0|   0|___A|   0|   0|   0|___G)
#define PAGE_SHARED	     __pg(__PP|__RW|_USR|___A|__NX|   0|   0|   0)
#define PAGE_SHARED_EXEC     __pg(__PP|__RW|_USR|___A|   0|   0|   0|   0)
#define PAGE_COPY_EXEC	     __pg(__PP|   0|_USR|___A|   0|   0|   0|   0)
#define PAGE_COPY	     __pg(__PP|   0|_USR|___A|__NX|   0|   0|   0)
#define PAGE_READONLY	     __pg(__PP|   0|_USR|___A|__NX|   0|   0|   0)
#define PAGE_READONLY_EXEC   __pg(__PP|   0|_USR|___A|   0|   0|   0|   0)

#define __PAGE_KERNEL		 (__PP|__RW|   0|___A|__NX|___D|   0|___G)
#define __PAGE_KERNEL_EXEC	 (__PP|__RW|   0|___A|   0|___D|   0|___G)
#define _KERNPG_TABLE		 (__PP|__RW|   0|___A|   0|___D|   0|   0| _ENC)
#define _PAGE_TABLE		 (__PP|__RW|_USR|___A|   0|___D|   0|   0| _ENC)
#define __PAGE_KERNEL_RO	 (__PP|   0|   0|___A|__NX|___D|   0|___G)
#define __PAGE_KERNEL_LARGE	 (__PP|__RW|   0|___A|__NX|___D|_PSE|___G)
#define __PAGE_KERNEL_LARGE_EXEC (__PP|__RW|   0|___A|   0|___D|_PSE|___G)

#ifndef __ASSEMBLY__

#define __pgprot_mask(x)	__pgprot((x) & __default_kernel_pte_mask)

#define PAGE_KERNEL		__pgprot_mask(__PAGE_KERNEL            | _ENC)
#define PAGE_KERNEL_RO		__pgprot_mask(__PAGE_KERNEL_RO         | _ENC)
#define PAGE_KERNEL_EXEC	__pgprot_mask(__PAGE_KERNEL_EXEC       | _ENC)
#define PAGE_KERNEL_LARGE	__pgprot_mask(__PAGE_KERNEL_LARGE      | _ENC)
#define PAGE_KERNEL_LARGE_EXEC	__pgprot_mask(__PAGE_KERNEL_LARGE_EXEC | _ENC)

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

#ifndef __ASSEMBLY__
#include <linux/types.h>

typedef unsigned long	pteval_t;
typedef unsigned long	pmdval_t;
typedef unsigned long	pgdval_t;
typedef unsigned long	pgprotval_t;

typedef union {
	pteval_t pte;
	pteval_t pte_low;
} pte_t;
#endif

#define ARCH_PAGE_TABLE_SYNC_MASK	PGTBL_PMD_MODIFIED
#define PGDIR_SHIFT	22
#define PTRS_PER_PGD	1024
#define PTRS_PER_PTE	1024

#define pgtable_l5_enabled() 0

#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE - 1))

#ifndef __ASSEMBLY__

#include <linux/types.h>

#define PTE_PFN_MASK		((pteval_t)PHYSICAL_PAGE_MASK)

#define PTE_FLAGS_MASK		(~PTE_PFN_MASK)

typedef struct pgprot { pgprotval_t pgprot; } pgprot_t;

typedef struct { pgdval_t pgd; } pgd_t;

#define PGD_ALLOWED_BITS	(~0ULL)

static inline pgd_t native_make_pgd(pgdval_t val)
{
	return (pgd_t) { val & PGD_ALLOWED_BITS };
}

static inline pgdval_t native_pgd_val(pgd_t pgd)
{
	return pgd.pgd & PGD_ALLOWED_BITS;
}

#define __PAGETABLE_P4D_FOLDED 1

typedef struct { pgd_t pgd; } p4d_t;

#define P4D_SHIFT		PGDIR_SHIFT
#define P4D_SIZE		(1UL << P4D_SHIFT)
#define P4D_MASK		(~(P4D_SIZE-1))

#define p4d_ERROR(p4d)				(pgd_ERROR((p4d).pgd))

static inline p4d_t *p4d_offset(pgd_t *pgd, unsigned long address)
{
	return (p4d_t *)pgd;
}

#define p4d_val(x)				(pgd_val((x).pgd))
#define __p4d(x)				((p4d_t) { __pgd(x) })

#define p4d_free_tlb(tlb, x, a)			do { } while (0)

#undef  p4d_addr_end
#define p4d_addr_end(addr, end)			(end)

#define __PAGETABLE_PUD_FOLDED 1

typedef struct { p4d_t p4d; } pud_t;

#define PUD_SHIFT	P4D_SHIFT
#define PUD_SIZE  	(1UL << PUD_SHIFT)
#define PUD_MASK  	(~(PUD_SIZE-1))

#define pud_ERROR(pud)				(p4d_ERROR((pud).p4d))

#define set_p4d(p4dptr, p4dval)	set_pud((pud_t *)(p4dptr), (pud_t) { p4dval })

static inline pud_t *pud_offset(p4d_t *p4d, unsigned long address)
{
	return (pud_t *)p4d;
}
#define pud_offset pud_offset

#define pud_val(x)				(p4d_val((x).p4d))
#define __pud(x)				((pud_t) { __p4d(x) })

#define pud_free_tlb(tlb, x, a)		        do { } while (0)

#undef  pud_addr_end
#define pud_addr_end(addr, end)			(end)

#define __PAGETABLE_PMD_FOLDED 1

typedef struct { pud_t pud; } pmd_t;

#define PMD_SHIFT	PUD_SHIFT
#define PTRS_PER_PMD	1
#define PMD_SIZE  	(1UL << PMD_SHIFT)
#define PMD_MASK  	(~(PMD_SIZE-1))

#define pmd_ERROR(pmd)				(pud_ERROR((pmd).pud))

#define set_pud(pudptr, pudval)			set_pmd((pmd_t *)(pudptr), (pmd_t) { pudval })

static inline pmd_t * pmd_offset(pud_t * pud, unsigned long address)
{
	return (pmd_t *)pud;
}
#define pmd_offset pmd_offset

#define pmd_val(x)				(pud_val((x).pud))
#define __pmd(x)				((pmd_t) { __pud(x) } )

#define pmd_free_tlb(tlb, x, a)		do { } while (0)

#undef  pmd_addr_end
#define pmd_addr_end(addr, end)			(end)

static inline pmdval_t native_pmd_val(pmd_t pmd)
{
	return native_pgd_val(pmd.pud.p4d.pgd);
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

unsigned long cachemode2protval(enum page_cache_mode pcm);

typedef struct page *pgtable_t;

extern pteval_t __supported_pte_mask;
extern pteval_t __default_kernel_pte_mask;

void set_pte_vaddr(unsigned long vaddr, pte_t pte);

extern void native_pagetable_init(void);

enum pg_level {
	PG_LEVEL_NONE,
	PG_LEVEL_4K,
	PG_LEVEL_2M,
	PG_LEVEL_1G,
	PG_LEVEL_512G,
	PG_LEVEL_NUM
};

#endif 

#endif  
