#ifndef _LINUX_PFN_T_H_
#define _LINUX_PFN_T_H_
#include <linux/mm.h>

#define PFN_FLAGS_MASK (((u64) (~PAGE_MASK)) << (BITS_PER_LONG_LONG - PAGE_SHIFT))
#define PFN_SG_CHAIN (1ULL << (BITS_PER_LONG_LONG - 1))
#define PFN_SG_LAST (1ULL << (BITS_PER_LONG_LONG - 2))
#define PFN_DEV (1ULL << (BITS_PER_LONG_LONG - 3))
#define PFN_MAP (1ULL << (BITS_PER_LONG_LONG - 4))
#define PFN_SPECIAL (1ULL << (BITS_PER_LONG_LONG - 5))

#define PFN_FLAGS_TRACE \
	{ PFN_SPECIAL,	"SPECIAL" }, \
	{ PFN_SG_CHAIN,	"SG_CHAIN" }, \
	{ PFN_SG_LAST,	"SG_LAST" }, \
	{ PFN_DEV,	"DEV" }, \
	{ PFN_MAP,	"MAP" }

static inline pfn_t __pfn_to_pfn_t(unsigned long pfn, u64 flags)
{
	pfn_t pfn_t = { .val = pfn | (flags & PFN_FLAGS_MASK), };

	return pfn_t;
}

static inline pfn_t pfn_to_pfn_t(unsigned long pfn)
{
	return __pfn_to_pfn_t(pfn, 0);
}

/* phys_to_pfn_t removed - never called */

static inline bool pfn_t_has_page(pfn_t pfn)
{
	return (pfn.val & PFN_MAP) == PFN_MAP || (pfn.val & PFN_DEV) == 0;
}

static inline unsigned long pfn_t_to_pfn(pfn_t pfn)
{
	return pfn.val & ~PFN_FLAGS_MASK;
}

/* pfn_t_to_page removed - never called */
/* pfn_t_to_phys removed - never called */
/* page_to_pfn_t removed - never called */
/* pfn_t_valid removed - never called */

static inline pte_t pfn_t_pte(pfn_t pfn, pgprot_t pgprot)
{
	return pfn_pte(pfn_t_to_pfn(pfn), pgprot);
}


static inline bool pfn_t_devmap(pfn_t pfn)
{
	return false;
}
pte_t pte_mkdevmap(pte_t pte);
pmd_t pmd_mkdevmap(pmd_t pmd);
#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && \
	defined(CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD)
pud_t pud_mkdevmap(pud_t pud);
#endif

/* pfn_t_special removed - never called */
#endif  
