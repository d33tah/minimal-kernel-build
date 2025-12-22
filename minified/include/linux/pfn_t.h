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

static inline unsigned long pfn_t_to_pfn(pfn_t pfn)
{
	return pfn.val & ~PFN_FLAGS_MASK;
}


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
/* CONFIG_TRANSPARENT_HUGEPAGE not defined */

#endif  
