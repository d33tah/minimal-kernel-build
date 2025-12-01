
#include <linux/memblock.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mmiotrace.h>
#include <linux/cc_platform.h>
#include <linux/efi.h>
#include <linux/pgtable.h>

#include <asm/set_memory.h>
#include <asm/e820/api.h>
#include <asm/efi.h>
#include <asm/fixmap.h>
#include <asm/tlbflush.h>
#include <asm/pgalloc.h>
#include <asm/memtype.h>
#include <asm/setup.h>

#include "physaddr.h"

struct ioremap_desc {
	unsigned int flags;
};

/* Stub: ioremap_change_attr not used in minimal kernel */
int ioremap_change_attr(unsigned long vaddr, unsigned long size,
			enum page_cache_mode pcm)
{
	return 0;
}

static unsigned int __ioremap_check_ram(struct resource *res)
{
	unsigned long start_pfn, stop_pfn;
	unsigned long i;

	if ((res->flags & IORESOURCE_SYSTEM_RAM) != IORESOURCE_SYSTEM_RAM)
		return 0;

	start_pfn = (res->start + PAGE_SIZE - 1) >> PAGE_SHIFT;
	stop_pfn = (res->end + 1) >> PAGE_SHIFT;
	if (stop_pfn > start_pfn) {
		for (i = 0; i < (stop_pfn - start_pfn); ++i)
			if (pfn_valid(start_pfn + i) &&
			    !PageReserved(pfn_to_page(start_pfn + i)))
				return IORES_MAP_SYSTEM_RAM;
	}

	return 0;
}

static unsigned int __ioremap_check_encrypted(struct resource *res)
{
	if (!cc_platform_has(CC_ATTR_GUEST_MEM_ENCRYPT))
		return 0;

	switch (res->desc) {
	case IORES_DESC_NONE:
	case IORES_DESC_RESERVED:
		break;
	default:
		return IORES_MAP_ENCRYPTED;
	}

	return 0;
}

static void __ioremap_check_other(resource_size_t addr, struct ioremap_desc *desc)
{
	if (!cc_platform_has(CC_ATTR_GUEST_MEM_ENCRYPT))
		return;

	if (!IS_ENABLED(CONFIG_EFI))
		return;

	if (efi_mem_type(addr) == EFI_RUNTIME_SERVICES_DATA ||
	    (efi_mem_type(addr) == EFI_BOOT_SERVICES_DATA &&
	     efi_mem_attributes(addr) & EFI_MEMORY_RUNTIME))
		desc->flags |= IORES_MAP_ENCRYPTED;
}

static int __ioremap_collect_map_flags(struct resource *res, void *arg)
{
	struct ioremap_desc *desc = arg;

	if (!(desc->flags & IORES_MAP_SYSTEM_RAM))
		desc->flags |= __ioremap_check_ram(res);

	if (!(desc->flags & IORES_MAP_ENCRYPTED))
		desc->flags |= __ioremap_check_encrypted(res);

	return ((desc->flags & (IORES_MAP_SYSTEM_RAM | IORES_MAP_ENCRYPTED)) ==
			       (IORES_MAP_SYSTEM_RAM | IORES_MAP_ENCRYPTED));
}

static void __ioremap_check_mem(resource_size_t addr, unsigned long size,
				struct ioremap_desc *desc)
{
	u64 start, end;

	start = (u64)addr;
	end = start + size - 1;
	memset(desc, 0, sizeof(struct ioremap_desc));

	walk_mem_res(start, end, desc, __ioremap_collect_map_flags);

	__ioremap_check_other(addr, desc);
}

static void __iomem *
__ioremap_caller(resource_size_t phys_addr, unsigned long size,
		 enum page_cache_mode pcm, void *caller, bool encrypted)
{
	unsigned long offset, vaddr;
	resource_size_t last_addr;
	const resource_size_t unaligned_phys_addr = phys_addr;
	const unsigned long unaligned_size = size;
	struct ioremap_desc io_desc;
	struct vm_struct *area;
	enum page_cache_mode new_pcm;
	pgprot_t prot;
	int retval;
	void __iomem *ret_addr;

	 
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	if (!phys_addr_valid(phys_addr)) {
		printk(KERN_WARNING "ioremap: invalid physical address %llx\n",
		       (unsigned long long)phys_addr);
		WARN_ON_ONCE(1);
		return NULL;
	}

	__ioremap_check_mem(phys_addr, size, &io_desc);

	 
	if (io_desc.flags & IORES_MAP_SYSTEM_RAM) {
		WARN_ONCE(1, "ioremap on RAM at %pa - %pa\n",
			  &phys_addr, &last_addr);
		return NULL;
	}

	 
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PHYSICAL_PAGE_MASK;
	size = PAGE_ALIGN(last_addr+1) - phys_addr;

	retval = memtype_reserve(phys_addr, (u64)phys_addr + size,
						pcm, &new_pcm);
	if (retval) {
		printk(KERN_ERR "ioremap memtype_reserve failed %d\n", retval);
		return NULL;
	}

	if (pcm != new_pcm) {
		if (!is_new_memtype_allowed(phys_addr, size, pcm, new_pcm)) {
			printk(KERN_ERR
		"ioremap error for 0x%llx-0x%llx, requested 0x%x, got 0x%x\n",
				(unsigned long long)phys_addr,
				(unsigned long long)(phys_addr + size),
				pcm, new_pcm);
			goto err_free_memtype;
		}
		pcm = new_pcm;
	}

	 
	prot = PAGE_KERNEL_IO;
	if ((io_desc.flags & IORES_MAP_ENCRYPTED) || encrypted)
		prot = pgprot_encrypted(prot);
	else
		prot = pgprot_decrypted(prot);

	switch (pcm) {
	case _PAGE_CACHE_MODE_UC:
	default:
		prot = __pgprot(pgprot_val(prot) |
				cachemode2protval(_PAGE_CACHE_MODE_UC));
		break;
	case _PAGE_CACHE_MODE_UC_MINUS:
		prot = __pgprot(pgprot_val(prot) |
				cachemode2protval(_PAGE_CACHE_MODE_UC_MINUS));
		break;
	case _PAGE_CACHE_MODE_WC:
		prot = __pgprot(pgprot_val(prot) |
				cachemode2protval(_PAGE_CACHE_MODE_WC));
		break;
	case _PAGE_CACHE_MODE_WT:
		prot = __pgprot(pgprot_val(prot) |
				cachemode2protval(_PAGE_CACHE_MODE_WT));
		break;
	case _PAGE_CACHE_MODE_WB:
		break;
	}

	 
	area = get_vm_area_caller(size, VM_IOREMAP, caller);
	if (!area)
		goto err_free_memtype;
	area->phys_addr = phys_addr;
	vaddr = (unsigned long) area->addr;

	if (memtype_kernel_map_sync(phys_addr, size, pcm))
		goto err_free_area;

	if (ioremap_page_range(vaddr, vaddr + size, phys_addr, prot))
		goto err_free_area;

	ret_addr = (void __iomem *) (vaddr + offset);
	mmiotrace_ioremap(unaligned_phys_addr, unaligned_size, ret_addr);

	 
	if (iomem_map_sanity_check(unaligned_phys_addr, unaligned_size))
		pr_warn("caller %pS mapping multiple BARs\n", caller);

	return ret_addr;
err_free_area:
	free_vm_area(area);
err_free_memtype:
	memtype_free(phys_addr, phys_addr + size);
	return NULL;
}

void __iomem *ioremap(resource_size_t phys_addr, unsigned long size)
{
	 
	enum page_cache_mode pcm = _PAGE_CACHE_MODE_UC_MINUS;

	return __ioremap_caller(phys_addr, size, pcm,
				__builtin_return_address(0), false);
}

void __iomem *ioremap_uc(resource_size_t phys_addr, unsigned long size)
{
	enum page_cache_mode pcm = _PAGE_CACHE_MODE_UC;

	return __ioremap_caller(phys_addr, size, pcm,
				__builtin_return_address(0), false);
}

void __iomem *ioremap_wc(resource_size_t phys_addr, unsigned long size)
{
	return __ioremap_caller(phys_addr, size, _PAGE_CACHE_MODE_WC,
					__builtin_return_address(0), false);
}

void __iomem *ioremap_wt(resource_size_t phys_addr, unsigned long size)
{
	return __ioremap_caller(phys_addr, size, _PAGE_CACHE_MODE_WT,
					__builtin_return_address(0), false);
}

/* Stub: ioremap_encrypted not used in minimal kernel */
void __iomem *ioremap_encrypted(resource_size_t phys_addr, unsigned long size)
{ return NULL; }

void __iomem *ioremap_cache(resource_size_t phys_addr, unsigned long size)
{
	return __ioremap_caller(phys_addr, size, _PAGE_CACHE_MODE_WB,
				__builtin_return_address(0), false);
}

/* Stub: ioremap_prot not used in minimal kernel */
void __iomem *ioremap_prot(resource_size_t phys_addr, unsigned long size,
				unsigned long prot_val)
{ return NULL; }

void iounmap(volatile void __iomem *addr)
{
	struct vm_struct *p, *o;

	if ((void __force *)addr <= high_memory)
		return;

	 
	if ((void __force *)addr >= phys_to_virt(ISA_START_ADDRESS) &&
	    (void __force *)addr < phys_to_virt(ISA_END_ADDRESS)) {
		WARN(1, "iounmap() called for ISA range not obtained using ioremap()\n");
		return;
	}

	mmiotrace_iounmap(addr);

	addr = (volatile void __iomem *)
		(PAGE_MASK & (unsigned long __force)addr);

	 
	p = find_vm_area((void __force *)addr);

	if (!p) {
		printk(KERN_ERR "iounmap: bad address %p\n", addr);
		dump_stack();
		return;
	}

	memtype_free(p->phys_addr, p->phys_addr + get_vm_area_size(p));

	 
	o = remove_vm_area((void __force *)addr);
	BUG_ON(p != o || o == NULL);
	kfree(p);
}

void *xlate_dev_mem_ptr(phys_addr_t phys)
{
	unsigned long start  = phys &  PAGE_MASK;
	unsigned long offset = phys & ~PAGE_MASK;
	void *vaddr;

	 
	vaddr = memremap(start, PAGE_SIZE, MEMREMAP_WB);

	 
	if (vaddr)
		vaddr += offset;

	return vaddr;
}

void unxlate_dev_mem_ptr(phys_addr_t phys, void *addr)
{
	memunmap((void *)((unsigned long)addr & PAGE_MASK));
}


static pte_t bm_pte[PAGE_SIZE/sizeof(pte_t)] __page_aligned_bss;

static inline pmd_t * __init early_ioremap_pmd(unsigned long addr)
{
	 
	pgd_t *base = __va(read_cr3_pa());
	pgd_t *pgd = &base[pgd_index(addr)];
	p4d_t *p4d = p4d_offset(pgd, addr);
	pud_t *pud = pud_offset(p4d, addr);
	pmd_t *pmd = pmd_offset(pud, addr);

	return pmd;
}

static inline pte_t * __init early_ioremap_pte(unsigned long addr)
{
	return &bm_pte[pte_index(addr)];
}

bool __init is_early_ioremap_ptep(pte_t *ptep)
{
	return ptep >= &bm_pte[0] && ptep < &bm_pte[PAGE_SIZE/sizeof(pte_t)];
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
	BUILD_BUG_ON((__fix_to_virt(FIX_BTMAP_BEGIN) >> PMD_SHIFT)
		     != (__fix_to_virt(FIX_BTMAP_END) >> PMD_SHIFT));
#undef __FIXADDR_TOP
	if (pmd != early_ioremap_pmd(fix_to_virt(FIX_BTMAP_END))) {
		WARN_ON(1);
		printk(KERN_WARNING "pmd %p != %p\n",
		       pmd, early_ioremap_pmd(fix_to_virt(FIX_BTMAP_END)));
		printk(KERN_WARNING "fix_to_virt(FIX_BTMAP_BEGIN): %08lx\n",
			fix_to_virt(FIX_BTMAP_BEGIN));
		printk(KERN_WARNING "fix_to_virt(FIX_BTMAP_END):   %08lx\n",
			fix_to_virt(FIX_BTMAP_END));

		printk(KERN_WARNING "FIX_BTMAP_END:       %d\n", FIX_BTMAP_END);
		printk(KERN_WARNING "FIX_BTMAP_BEGIN:     %d\n",
		       FIX_BTMAP_BEGIN);
	}
}

void __init __early_set_fixmap(enum fixed_addresses idx,
			       phys_addr_t phys, pgprot_t flags)
{
	unsigned long addr = __fix_to_virt(idx);
	pte_t *pte;

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}
	pte = early_ioremap_pte(addr);

	 
	pgprot_val(flags) &= __supported_pte_mask;

	if (pgprot_val(flags))
		set_pte(pte, pfn_pte(phys >> PAGE_SHIFT, flags));
	else
		pte_clear(&init_mm, addr, pte);
	flush_tlb_one_kernel(addr);
}
