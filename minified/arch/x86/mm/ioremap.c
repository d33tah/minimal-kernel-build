
#include <linux/memblock.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/pgtable.h>

#include <asm/e820/api.h>
#include <asm/fixmap.h>
#include <asm/tlbflush.h>
#include <asm/pgalloc.h>
#include <asm/setup.h>

static pte_t bm_pte[PAGE_SIZE / sizeof(pte_t)] __page_aligned_bss;

static inline pmd_t *__init early_ioremap_pmd(unsigned long addr)
{
	pgd_t *base = __va(read_cr3_pa());
	pgd_t *pgd = &base[pgd_index(addr)];
	p4d_t *p4d = p4d_offset(pgd, addr);
	pud_t *pud = pud_offset(p4d, addr);
	pmd_t *pmd = pmd_offset(pud, addr);

	return pmd;
}

void __init early_ioremap_init(void)
{
	pmd_t *pmd;

	WARN_ON((fix_to_virt(0) + PAGE_SIZE) & ((1 << PMD_SHIFT) - 1));

	early_ioremap_setup();

	pmd = early_ioremap_pmd(fix_to_virt(FIX_BTMAP_BEGIN));
	memset(bm_pte, 0, sizeof(bm_pte));
	pmd_populate_kernel(&init_mm, pmd, bm_pte);

#define __FIXADDR_TOP (-PAGE_SIZE)
	BUILD_BUG_ON((__fix_to_virt(FIX_BTMAP_BEGIN) >> PMD_SHIFT) !=
		     (__fix_to_virt(FIX_BTMAP_END) >> PMD_SHIFT));
#undef __FIXADDR_TOP
	if (pmd != early_ioremap_pmd(fix_to_virt(FIX_BTMAP_END))) {
		WARN_ON(1);
		printk(KERN_WARNING "pmd %p != %p\n", pmd,
		       early_ioremap_pmd(fix_to_virt(FIX_BTMAP_END)));
		printk(KERN_WARNING "fix_to_virt(FIX_BTMAP_BEGIN): %08lx\n",
		       fix_to_virt(FIX_BTMAP_BEGIN));
		printk(KERN_WARNING "fix_to_virt(FIX_BTMAP_END):   %08lx\n",
		       fix_to_virt(FIX_BTMAP_END));

		printk(KERN_WARNING "FIX_BTMAP_END:       %d\n", FIX_BTMAP_END);
		printk(KERN_WARNING "FIX_BTMAP_BEGIN:     %d\n",
		       FIX_BTMAP_BEGIN);
	}
}

void __init __early_set_fixmap(enum fixed_addresses idx, phys_addr_t phys,
			       pgprot_t flags)
{
	unsigned long addr = __fix_to_virt(idx);
	pte_t *pte;

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}
	pte = &bm_pte[pte_index(addr)];

	pgprot_val(flags) &= __supported_pte_mask;

	if (pgprot_val(flags))
		set_pte(pte, pfn_pte(phys >> PAGE_SHIFT, flags));
	else
		pte_clear(&init_mm, addr, pte);
	flush_tlb_one_kernel(addr);
}
