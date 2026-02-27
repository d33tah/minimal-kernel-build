
#include <asm/io.h>

#include <asm/pgalloc.h>

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

	early_ioremap_setup();

	pmd = early_ioremap_pmd(fix_to_virt(FIX_BTMAP_BEGIN));
	memset(bm_pte, 0, sizeof(bm_pte));
	pmd_populate_kernel(&init_mm, pmd, bm_pte);
}
