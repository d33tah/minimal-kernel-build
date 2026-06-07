/* --- 2025-12-07 20:44 --- Inlined iomap.h */
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/highmem.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
void __iomem *__iomap_local_pfn_prot(unsigned long pfn, pgprot_t prot);
int iomap_create_wc(resource_size_t base, unsigned long size, pgprot_t *prot);
#include <asm/memtype.h>
#include <linux/export.h>

static int is_io_mapping_possible(resource_size_t base, unsigned long size)
{
	return 1;
}

int iomap_create_wc(resource_size_t base, unsigned long size, pgprot_t *prot)
{
	enum page_cache_mode pcm = _PAGE_CACHE_MODE_WC;
	int ret;

	if (!is_io_mapping_possible(base, size))
		return -EINVAL;

	ret = memtype_reserve_io(base, base + size, &pcm);
	if (ret)
		return ret;

	*prot = __pgprot(__PAGE_KERNEL | cachemode2protval(pcm));
	 
	pgprot_val(*prot) &= __default_kernel_pte_mask;

	return 0;
}


void __iomem *__iomap_local_pfn_prot(unsigned long pfn, pgprot_t prot)
{
	 
	if (!pat_enabled() && pgprot2cachemode(prot) != _PAGE_CACHE_MODE_WB)
		prot = __pgprot(__PAGE_KERNEL |
				cachemode2protval(_PAGE_CACHE_MODE_UC_MINUS));

	 
	pgprot_val(prot) &= __default_kernel_pte_mask;

	return (void __force __iomem *)__kmap_local_pfn_prot(pfn, prot);
}
