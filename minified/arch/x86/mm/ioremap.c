// SPDX-License-Identifier: GPL-2.0-only
/*
 * Re-map IO memory to kernel address space so that we can access it.
 * This is needed for high PCI addresses that aren't mapped in the
 * 640k-1MB IO memory area on PC's
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 */

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

/*
 * Descriptor controlling ioremap() behavior.
 */
struct ioremap_desc {
	unsigned int flags;
};

/*
 * Fix up the linear direct mapping of the kernel to avoid cache attribute
 * conflicts.
 */
int ioremap_change_attr(unsigned long vaddr, unsigned long size,
			enum page_cache_mode pcm)
{
	unsigned long nrpages = size >> PAGE_SHIFT;
	int err;

	switch (pcm) {
	case _PAGE_CACHE_MODE_UC:
	default:
		err = _set_memory_uc(vaddr, nrpages);
		break;
	case _PAGE_CACHE_MODE_WC:
		err = _set_memory_wc(vaddr, nrpages);
		break;
	case _PAGE_CACHE_MODE_WT:
		err = _set_memory_wt(vaddr, nrpages);
		break;
	case _PAGE_CACHE_MODE_WB:
		err = _set_memory_wb(vaddr, nrpages);
		break;
	}

	return err;
}

/* Does the range (or a subset of) contain normal RAM? */
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

/*
 * In a SEV guest, NONE and RESERVED should not be mapped encrypted because
 * there the whole memory is already encrypted.
 */
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

/*
 * The EFI runtime services data area is not covered by walk_mem_res(), but must
 * be mapped encrypted when SEV is active.
 */
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

/*
 * To avoid multiple resource walks, this function walks resources marked as
 * IORESOURCE_MEM and IORESOURCE_BUSY and looking for system RAM and/or a
 * resource described not as IORES_DESC_NONE (e.g. IORES_DESC_ACPI_TABLES).
 *
 * After that, deal with misc other ranges in __ioremap_check_other() which do
 * not fall into the above category.
 */
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

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. It transparently creates kernel huge I/O mapping when
 * the physical address is aligned by a huge page size (1GB or 2MB) and
 * the requested size is at least the huge page size.
 *
 * NOTE: MTRRs can override PAT memory types with a 4KB granularity.
 * Therefore, the mapping code falls back to use a smaller page toward 4KB
 * when a mapping range is covered by non-WB type of MTRRs.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */
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

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	__ioremap_check_mem(phys_addr, size, &io_desc);

	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PHYSICAL_PAGE_MASK;
	size = PAGE_ALIGN(last_addr+1) - phys_addr;

	retval = memtype_reserve(phys_addr, (u64)phys_addr + size,
						pcm, &new_pcm);
	prot = PAGE_KERNEL_IO;
	area = get_vm_area_caller(size, VM_IOREMAP, caller);
	area->phys_addr = phys_addr;
	vaddr = (unsigned long) area->addr;

	ioremap_page_range(vaddr, vaddr + size, phys_addr, prot);

	ret_addr = (void __iomem *) (vaddr + offset);
	mmiotrace_ioremap(unaligned_phys_addr, unaligned_size, ret_addr);

	if (iomem_map_sanity_check(unaligned_phys_addr, unaligned_size))
		pr_warn("caller %pS mapping multiple BARs\n", caller);

	return ret_addr;
}

/**
 * ioremap     -   map bus memory into CPU space
 * @phys_addr:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * This version of ioremap ensures that the memory is marked uncachable
 * on the CPU as well as honouring existing caching rules from things like
 * the PCI bus. Note that there are other caches and buffers on many
 * busses. In particular driver authors should read up on PCI writes
 *
 * It's useful if some control registers are in such an area and
 * write combining or read caching is not desirable:
 *
 * Must be freed with iounmap.
 */
void __iomem *ioremap(resource_size_t phys_addr, unsigned long size)
{
	/*
	 * Ideally, this should be:
	 *	pat_enabled() ? _PAGE_CACHE_MODE_UC : _PAGE_CACHE_MODE_UC_MINUS;
	 *
	 * Till we fix all X drivers to use ioremap_wc(), we will use
	 * UC MINUS. Drivers that are certain they need or can already
	 * be converted over to strong UC can use ioremap_uc().
	 */
	enum page_cache_mode pcm = _PAGE_CACHE_MODE_UC_MINUS;

	return __ioremap_caller(phys_addr, size, pcm,
				__builtin_return_address(0), false);
}
EXPORT_SYMBOL(ioremap);

/**
 * ioremap_uc     -   map bus memory into CPU space as strongly uncachable
 * @phys_addr:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap_uc performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * This version of ioremap ensures that the memory is marked with a strong
 * preference as completely uncachable on the CPU when possible. For non-PAT
 * systems this ends up setting page-attribute flags PCD=1, PWT=1. For PAT
 * systems this will set the PAT entry for the pages as strong UC.  This call
 * will honor existing caching rules from things like the PCI bus. Note that
 * there are other caches and buffers on many busses. In particular driver
 * authors should read up on PCI writes.
 *
 * It's useful if some control registers are in such an area and
 * write combining or read caching is not desirable:
 *
 * Must be freed with iounmap.
 */
void __iomem *ioremap_uc(resource_size_t phys_addr, unsigned long size)
{
	enum page_cache_mode pcm = _PAGE_CACHE_MODE_UC;

	return __ioremap_caller(phys_addr, size, pcm,
				__builtin_return_address(0), false);
}
EXPORT_SYMBOL_GPL(ioremap_uc);

/**
 * ioremap_wc	-	map memory into CPU space write combined
 * @phys_addr:	bus address of the memory
 * @size:	size of the resource to map
 *
 * This version of ioremap ensures that the memory is marked write combining.
 * Write combining allows faster writes to some hardware devices.
 *
 * Must be freed with iounmap.
 */
void __iomem *ioremap_wc(resource_size_t phys_addr, unsigned long size)
{
	return __ioremap_caller(phys_addr, size, _PAGE_CACHE_MODE_WC,
					__builtin_return_address(0), false);
}
EXPORT_SYMBOL(ioremap_wc);

/**
 * ioremap_wt	-	map memory into CPU space write through
 * @phys_addr:	bus address of the memory
 * @size:	size of the resource to map
 *
 * This version of ioremap ensures that the memory is marked write through.
 * Write through stores data into memory while keeping the cache up-to-date.
 *
 * Must be freed with iounmap.
 */
void __iomem *ioremap_wt(resource_size_t phys_addr, unsigned long size)
{
	return __ioremap_caller(phys_addr, size, _PAGE_CACHE_MODE_WT,
					__builtin_return_address(0), false);
}
EXPORT_SYMBOL(ioremap_wt);

void __iomem *ioremap_encrypted(resource_size_t phys_addr, unsigned long size)
{
	return __ioremap_caller(phys_addr, size, _PAGE_CACHE_MODE_WB,
				__builtin_return_address(0), true);
}
EXPORT_SYMBOL(ioremap_encrypted);

void __iomem *ioremap_cache(resource_size_t phys_addr, unsigned long size)
{
	return __ioremap_caller(phys_addr, size, _PAGE_CACHE_MODE_WB,
				__builtin_return_address(0), false);
}
EXPORT_SYMBOL(ioremap_cache);

void __iomem *ioremap_prot(resource_size_t phys_addr, unsigned long size,
				unsigned long prot_val)
{
	return __ioremap_caller(phys_addr, size,
				pgprot2cachemode(__pgprot(prot_val)),
				__builtin_return_address(0), false);
}
EXPORT_SYMBOL(ioremap_prot);

/**
 * iounmap - Free a IO remapping
 * @addr: virtual address from ioremap_*
 *
 * Caller must ensure there is only one unmapping for the same pointer.
 */
void iounmap(volatile void __iomem *addr)
{
	struct vm_struct *p, *o;

	if ((void __force *)addr <= high_memory)
		return;

	/*
	 * The PCI/ISA range special-casing was removed from __ioremap()
	 * so this check, in theory, can be removed. However, there are
	 * cases where iounmap() is called for addresses not obtained via
	 * ioremap() (vga16fb for example). Add a warning so that these
	 * cases can be caught and fixed.
	 */
	if ((void __force *)addr >= phys_to_virt(ISA_START_ADDRESS) &&
	    (void __force *)addr < phys_to_virt(ISA_END_ADDRESS)) {
		WARN(1, "iounmap() called for ISA range not obtained using ioremap()\n");
		return;
	}

	mmiotrace_iounmap(addr);

	addr = (volatile void __iomem *)
		(PAGE_MASK & (unsigned long __force)addr);

	/* Use the vm area unlocked, assuming the caller
	   ensures there isn't another iounmap for the same address
	   in parallel. Reuse of the virtual address is prevented by
	   leaving it in the global lists until we're done with it.
	   cpa takes care of the direct mappings. */
	p = find_vm_area((void __force *)addr);

	if (!p) {
		printk(KERN_ERR "iounmap: bad address %p\n", addr);
		dump_stack();
		return;
	}

	memtype_free(p->phys_addr, p->phys_addr + get_vm_area_size(p));

	/* Finally remove it */
	o = remove_vm_area((void __force *)addr);
	BUG_ON(p != o || o == NULL);
	kfree(p);
}
EXPORT_SYMBOL(iounmap);

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
void *xlate_dev_mem_ptr(phys_addr_t phys)
{
	unsigned long start  = phys &  PAGE_MASK;
	unsigned long offset = phys & ~PAGE_MASK;
	void *vaddr;

	/* memremap() maps if RAM, otherwise falls back to ioremap() */
	vaddr = memremap(start, PAGE_SIZE, MEMREMAP_WB);

	/* Only add the offset on success and return NULL if memremap() failed */
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
	/* Don't assume we're using swapper_pg_dir at this point */
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

	/*
	 * The boot-ioremap range spans multiple pmds, for which
	 * we are not prepared:
	 */
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

	/* Sanitize 'prot' against any unsupported bits: */
	pgprot_val(flags) &= __supported_pte_mask;

	if (pgprot_val(flags))
		set_pte(pte, pfn_pte(phys >> PAGE_SHIFT, flags));
	else
		pte_clear(&init_mm, addr, pte);
	flush_tlb_one_kernel(addr);
}
