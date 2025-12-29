// SPDX-License-Identifier: GPL-2.0
/*
 * EXPERIMENTAL: x86 NOMMU stub functions
 * These provide stub implementations for MMU-dependent functions
 */

#ifndef CONFIG_MMU

#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/mmzone.h>
#include <linux/memblock.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/percpu.h>
#include <asm/pgtable.h>
#include <asm/cpu_entry_area.h>
#include <asm/elf.h>
#include <asm/fixmap.h>
#include <asm/tlbflush.h>
#include <asm/special_insns.h>

/* paging_init is declared in pgtable_32.h/pgtable_64.h which we bypass for NOMMU */
extern void paging_init(void);
/* Note: swapper_pg_dir is #defined to initial_page_table in pgtable-nommu.h */

/* CPU Entry Area stubs */
struct cpu_entry_area *get_cpu_entry_area(int cpu)
{
	return NULL;
}
EXPORT_SYMBOL(get_cpu_entry_area);

/* Forward declaration */
void set_pte_vaddr(unsigned long vaddr, pte_t pteval);

void cea_set_pte(void *cea_vaddr, phys_addr_t pa, pgprot_t flags)
{
	unsigned long va = (unsigned long)cea_vaddr;
	pte_t pte = pfn_pte(pa >> PAGE_SHIFT, flags);
	set_pte_vaddr(va, pte);
}
EXPORT_SYMBOL(cea_set_pte);

/* Memory mapping stubs */
unsigned long __get_unmapped_area(struct file *file, unsigned long addr,
				  unsigned long len, unsigned long pgoff,
				  unsigned long flags, vm_flags_t vm_flags)
{
	return -ENOMEM;
}
EXPORT_SYMBOL(__get_unmapped_area);

struct vm_area_struct *
_install_special_mapping(struct mm_struct *mm, unsigned long addr,
			 unsigned long len, unsigned long vm_flags,
			 const struct vm_special_mapping *spec)
{
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL(_install_special_mapping);

bool vma_is_special_mapping(const struct vm_area_struct *vma,
			    const struct vm_special_mapping *sm)
{
	return false;
}
EXPORT_SYMBOL(vma_is_special_mapping);

/* Page table stubs */
pte_t *__get_locked_pte(struct mm_struct *mm, unsigned long addr,
			spinlock_t **ptl)
{
	return NULL;
}
EXPORT_SYMBOL(__get_locked_pte);

/* Memory protection stubs are in pat/set_memory.c */
/* __virt_addr_valid is in physaddr.c */

int _set_memory_uc(unsigned long addr, int numpages)
{
	return 0;
}
EXPORT_SYMBOL(_set_memory_uc);

int _set_memory_wc(unsigned long addr, int numpages)
{
	return 0;
}
EXPORT_SYMBOL(_set_memory_wc);

int _set_memory_wt(unsigned long addr, int numpages)
{
	return 0;
}
EXPORT_SYMBOL(_set_memory_wt);

int _set_memory_wb(unsigned long addr, int numpages)
{
	return 0;
}
EXPORT_SYMBOL(_set_memory_wb);

/* kmap stubs */
void *__kmap_local_pfn_prot(unsigned long pfn, pgprot_t prot)
{
	return NULL;
}
EXPORT_SYMBOL(__kmap_local_pfn_prot);

void __kmap_local_sched_out(void)
{
}
EXPORT_SYMBOL(__kmap_local_sched_out);

void __kmap_local_sched_in(void)
{
}
EXPORT_SYMBOL(__kmap_local_sched_in);

void kmap_local_fork(struct task_struct *tsk)
{
}
EXPORT_SYMBOL(kmap_local_fork);

/* TLB flush stubs */
void try_to_unmap_flush(void)
{
}
EXPORT_SYMBOL(try_to_unmap_flush);

void try_to_unmap_flush_dirty(void)
{
}
EXPORT_SYMBOL(try_to_unmap_flush_dirty);

/* pfnmap tracking stub */
void pfnmap_track_ctx_release(struct vm_area_struct *vma)
{
}
EXPORT_SYMBOL(pfnmap_track_ctx_release);

/* vmf insert stub */
vm_fault_t vmf_insert_pfn(struct vm_area_struct *vma, unsigned long addr,
			  unsigned long pfn)
{
	return VM_FAULT_SIGBUS;
}
EXPORT_SYMBOL(vmf_insert_pfn);

/* Fixed address top - same as pgtable_32.c default */
unsigned long __FIXADDR_TOP = 0xfffff000;
EXPORT_SYMBOL(__FIXADDR_TOP);

/* set_memory_x/rw/ro and set_pages_ro are in pat/set_memory.c */

/* TLB flush stub */
void flush_tlb_one_kernel(unsigned long addr)
{
	asm volatile("invlpg (%0)" ::"r"(addr) : "memory");
}
EXPORT_SYMBOL(flush_tlb_one_kernel);

/* Debug output to QEMU port 0xe9 */
static inline void nommu_dbg(const char *s)
{
	while (*s)
		asm volatile("outb %0, $0xe9" : : "a"(*s++));
}

/* Debug helper to print hex value */
static void nommu_dbg_hex(unsigned long val)
{
	char c;
	int i;
	for (i = 28; i >= 0; i -= 4) {
		c = (val >> i) & 0xF;
		c = c < 10 ? '0' + c : 'a' + c - 10;
		asm volatile("outb %0, $0xe9" : : "a"(c));
	}
}

/* zone_sizes_init - initialize memory zones */
static void __init zone_sizes_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));

	nommu_dbg("NOMMU: zone_sizes_init max_low_pfn=0x");
	nommu_dbg_hex(max_low_pfn);
	nommu_dbg("\n");

	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;

	nommu_dbg("NOMMU: calling free_area_init\n");

	free_area_init(max_zone_pfns);

	nommu_dbg("NOMMU: zone_sizes_init done\n");
}

/* paging_init stub - set up memory zones */
void __init paging_init(void)
{
	nommu_dbg("NOMMU: paging_init\n");
	zone_sizes_init();
}

/* alloc_low_pages - use simple bump allocation */
void *alloc_low_pages(unsigned int num)
{
	return NULL; /* nommu_map_range will handle the NULL */
}

/* mmap_init stub */
void mmap_init(void)
{
}
EXPORT_SYMBOL(mmap_init);

/* lazy TLB stub */
void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}
EXPORT_SYMBOL(enter_lazy_tlb);

/* TLB state - already declared in tlbflush.h, provide definition */
DEFINE_PER_CPU_ALIGNED(struct tlb_state, cpu_tlbstate);
DEFINE_PER_CPU_SHARED_ALIGNED(struct tlb_state_shared, cpu_tlbstate_shared);

/* anon_vma_init stub */
void anon_vma_init(void)
{
}

/* mem_init - free memblock pages to buddy allocator */
void __init mem_init(void)
{
	nommu_dbg("NOMMU: mem_init start\n");

	/* Free memblock pages to buddy allocator - this is critical */
	nommu_dbg("NOMMU: calling memblock_free_all\n");
	memblock_free_all();
	nommu_dbg("NOMMU: memblock_free_all done\n");

	nommu_dbg("NOMMU: mem_init done\n");
}

/* vmalloc_init stub */
void __init vmalloc_init(void)
{
	nommu_dbg("NOMMU: vmalloc_init\n");
}

/* mem_map - for simple kernel we don't use it */
struct page *mem_map;
EXPORT_SYMBOL(mem_map);

/* free_initrd_mem stub */
void free_initrd_mem(unsigned long start, unsigned long end)
{
}

/* Page table masks */
pteval_t __default_kernel_pte_mask __read_mostly = ~0;
EXPORT_SYMBOL(__default_kernel_pte_mask);

pteval_t __supported_pte_mask __read_mostly = ~0;
EXPORT_SYMBOL(__supported_pte_mask);

/* TLB flush all */
void __flush_tlb_all(void)
{
	unsigned long cr4 = native_read_cr4();
	native_write_cr4(cr4 ^ X86_CR4_PGE);
	native_write_cr4(cr4);
}
EXPORT_SYMBOL(__flush_tlb_all);

/* MAXMEM_PFN for 32-bit - 896MB */
#define MAXMEM_PFN ((896UL << 20) >> PAGE_SHIFT)

/* find_low_pfn_range - set max_low_pfn from max_pfn */
void __init find_low_pfn_range(void)
{
	nommu_dbg("NOMMU: find_low_pfn_range max_pfn=0x");
	nommu_dbg_hex(max_pfn);
	nommu_dbg("\n");

	/* Simple case: all RAM is lowmem if under 896MB */
	if (max_pfn <= MAXMEM_PFN) {
		max_low_pfn = max_pfn;
	} else {
		max_low_pfn = MAXMEM_PFN;
	}

	nommu_dbg("NOMMU: max_low_pfn set to 0x");
	nommu_dbg_hex(max_low_pfn);
	nommu_dbg("\n");
}

/* early_alloc_pgt_buf stub */
void __init early_alloc_pgt_buf(void)
{
}

/* init_mem_mapping stub */
void __init init_mem_mapping(void)
{
	nommu_dbg("NOMMU: init_mem_mapping\n");
}

/* initmem_init - set up memory for boot */
void __init initmem_init(void)
{
	nommu_dbg("NOMMU: initmem_init start\n");

	/* Set high_memory pointer */
	high_memory = (void *)__va(max_low_pfn * PAGE_SIZE - 1) + 1;

	/* Set memory node */
	memblock_set_node(0, PHYS_ADDR_MAX, &memblock.memory, 0);

	/* Set max_mapnr */
	max_mapnr = max_low_pfn;
	__vmalloc_start_set = true;

	nommu_dbg("NOMMU: initmem_init done\n");
}

/* sync_initial_page_table stub */
void sync_initial_page_table(void)
{
}

/* pfn_range_is_mapped stub */
bool pfn_range_is_mapped(unsigned long start_pfn, unsigned long end_pfn)
{
	return true; /* assume all mapped */
}

/* max_mapnr */
unsigned long max_mapnr;
EXPORT_SYMBOL(max_mapnr);

/* free_initmem stub */
void free_initmem(void)
{
}

/* mark_rodata_ro stub */
void mark_rodata_ro(void)
{
}

/* get_unmapped_area stub */
unsigned long get_unmapped_area(struct file *file, unsigned long addr,
				unsigned long len, unsigned long pgoff,
				unsigned long flags)
{
	return -ENOMEM;
}
EXPORT_SYMBOL(get_unmapped_area);

/* pgd_page_get_mm stub */
struct mm_struct *pgd_page_get_mm(struct page *page)
{
	return NULL;
}

/* high_memory */
void *high_memory;
EXPORT_SYMBOL(high_memory);

/* cachemode functions */
unsigned long cachemode2protval(enum page_cache_mode pcm)
{
	return 0;
}
EXPORT_SYMBOL(cachemode2protval);

bool __vmalloc_start_set;
EXPORT_SYMBOL(__vmalloc_start_set);

bool is_vmalloc_addr(const void *x)
{
	return false;
}
EXPORT_SYMBOL(is_vmalloc_addr);

/* protection_map - array of pgprot_t values */
pgprot_t protection_map[16] = { [0 ... 15] = __pgprot(0) };
EXPORT_SYMBOL(protection_map);

enum page_cache_mode pgprot2cachemode(pgprot_t pgprot)
{
	return _PAGE_CACHE_MODE_WB;
}
EXPORT_SYMBOL(pgprot2cachemode);

/* pgd alloc/free stubs */
pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return NULL;
}

void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
}

/* last_mm_ctx_id */
atomic64_t last_mm_ctx_id = ATOMIC64_INIT(1);
EXPORT_SYMBOL(last_mm_ctx_id);

/* VMA link stub */
void __vma_link_rb(struct mm_struct *mm, struct vm_area_struct *vma,
		   struct rb_node **rb_link, struct rb_node *rb_parent)
{
}

/* switch_mm stubs */
void switch_mm_irqs_off(struct mm_struct *prev, struct mm_struct *next,
			struct task_struct *tsk)
{
}
EXPORT_SYMBOL(switch_mm_irqs_off);

/* vmalloc stubs */
void vfree(const void *addr)
{
}
EXPORT_SYMBOL(vfree);

void vunmap(const void *addr)
{
}
EXPORT_SYMBOL(vunmap);

/* pmd_install stub */
int pmd_install(struct mm_struct *mm, pmd_t *pmd, pgtable_t *pte)
{
	return 0;
}

/* do_set_pte stub */
void do_set_pte(struct vm_fault *vmf, struct page *page, unsigned long addr)
{
}

/* unmap stubs */
void unmap_mapping_folio(struct folio *folio)
{
}

void unmap_mapping_pages(struct address_space *mapping, pgoff_t start,
			 pgoff_t nr, bool even_cows)
{
}

void unmap_mapping_range(struct address_space *mapping, loff_t holebegin,
			 loff_t holelen, int even_cows)
{
}
EXPORT_SYMBOL(unmap_mapping_range);

/* mlock stubs */
void mlock_new_page(struct page *page)
{
}

void mlock_page_drain_local(void)
{
}

void mlock_page_drain_remote(int cpu)
{
}

/* folio_mkclean stub */
int folio_mkclean(struct folio *folio)
{
	return 0;
}
EXPORT_SYMBOL(folio_mkclean);

/* do_mmap stub */
unsigned long do_mmap(struct file *file, unsigned long addr, unsigned long len,
		      unsigned long prot, unsigned long flags,
		      unsigned long pgoff, unsigned long *populate,
		      struct list_head *uf)
{
	return -ENOMEM;
}

/* vmalloc stubs */
void *__vmalloc_node_range(unsigned long size, unsigned long align,
			   unsigned long start, unsigned long end,
			   gfp_t gfp_mask, pgprot_t prot,
			   unsigned long vm_flags, int node, const void *caller)
{
	return NULL;
}

void *__vmalloc(unsigned long size, gfp_t gfp_mask)
{
	return NULL;
}
EXPORT_SYMBOL(__vmalloc);

/* highest_memmap_pfn */
unsigned long highest_memmap_pfn;
EXPORT_SYMBOL(highest_memmap_pfn);

/* mmap_min_addr */
unsigned long mmap_min_addr;
EXPORT_SYMBOL(mmap_min_addr);

/* TLB gather stubs */
void tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm)
{
}

void tlb_finish_mmu(struct mmu_gather *tlb)
{
}

/* expand_stack stub */
int expand_stack(struct vm_area_struct *vma, unsigned long address)
{
	return -ENOMEM;
}
EXPORT_SYMBOL(expand_stack);

/* __vma_adjust stub */
int __vma_adjust(struct vm_area_struct *vma, unsigned long start,
		 unsigned long end, pgoff_t pgoff,
		 struct vm_area_struct *insert, struct vm_area_struct *expand)
{
	return 0;
}

/* move_page_tables stub */
unsigned long move_page_tables(struct vm_area_struct *vma,
			       unsigned long old_addr,
			       struct vm_area_struct *new_vma,
			       unsigned long new_addr, unsigned long len,
			       bool need_rmap_locks)
{
	return 0;
}

/* free_pgd_range stub */
void free_pgd_range(struct mmu_gather *tlb, unsigned long addr,
		    unsigned long end, unsigned long floor,
		    unsigned long ceiling)
{
}

/* switch_mm stub */
void switch_mm(struct mm_struct *prev, struct mm_struct *next,
	       struct task_struct *tsk)
{
}

/* arch_pick_mmap_layout stub */
void arch_pick_mmap_layout(struct mm_struct *mm, struct rlimit *rlim_stack)
{
}

/* CPU entry area setup */
void setup_cpu_entry_areas(void)
{
}
EXPORT_SYMBOL(setup_cpu_entry_areas);

/* Include mm_internal.h for alloc_low_page */
#include "mm_internal.h"

/* Map a physical address range into kernel virtual space with PAGE_KERNEL flags */
static void __init nommu_map_range(unsigned long phys_start, unsigned long size)
{
	unsigned long vaddr, phys;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	for (phys = phys_start; phys < phys_start + size; phys += PAGE_SIZE) {
		vaddr = (unsigned long)__va(phys);

		pgd = swapper_pg_dir + pgd_index(vaddr);
		if (pgd_none(*pgd)) {
			pr_err("NOMMU: No PGD for vaddr %lx\n", vaddr);
			continue;
		}

		/* For 2-level tables, PMD is same as PGD */
		pmd = pmd_offset(pud_offset(p4d_offset(pgd, vaddr), vaddr),
				 vaddr);

		/* Allocate PTE table if needed */
		if (!(pmd_val(*pmd) & _PAGE_PRESENT)) {
			pte_t *page_table = (pte_t *)alloc_low_page();
			if (!page_table) {
				pr_err("NOMMU: Failed to alloc PTE table for %lx\n",
				       vaddr);
				continue;
			}
			set_pmd(pmd, __pmd(__pa(page_table) | _PAGE_TABLE));
		}

		/* Now set the PTE */
		pte = pte_offset_kernel(pmd, vaddr);
		*pte = pfn_pte(phys >> PAGE_SHIFT, PAGE_KERNEL);

		/* Flush TLB for this address */
		asm volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
	}
}

/* Pagetable init - must call paging_init() to set up zones and mem_map */
void __init native_pagetable_init(void)
{
	nommu_dbg("NOMMU: native_pagetable_init start\n");
	/* paging_init() sets up zones and allocates mem_map */
	paging_init();
	nommu_dbg("NOMMU: after paging_init\n");

	/*
	 * NOMMU: Map VGA text framebuffer at 0xb8000
	 * This is needed for VGA console output
	 */
	nommu_map_range(0xb8000, PAGE_SIZE * 2); /* 8KB = 2 pages */
	nommu_dbg("NOMMU: native_pagetable_init done\n");
}

/* va_align for AMD - define struct locally since elf.h may guard it */
struct va_alignment {
	int flags;
	unsigned long mask;
	unsigned long bits;
};
struct va_alignment va_align = {
	.flags = -1,
};
EXPORT_SYMBOL(va_align);

/* VMALLOC reserve */
unsigned int __VMALLOC_RESERVE = 0;
EXPORT_SYMBOL(__VMALLOC_RESERVE);

/*
 * Fixmap implementation for NOMMU.
 * We still have page tables on x86 even for NOMMU (paging cannot be disabled).
 * We need to actually set up the page table entries for fixmaps.
 */
/* Debug output for QEMU */
static inline void pte_dbg(char c)
{
	asm volatile("outb %0, $0xe9" : : "a"(c));
}

void set_pte_vaddr(unsigned long vaddr, pte_t pteval)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long pte_table_phys;

	pte_dbg('[');
	/* Show high nibble of vaddr to identify which region */
	pte_dbg('0' + ((vaddr >> 28) & 0xF));

	pgd = swapper_pg_dir + pgd_index(vaddr);
	if (pgd_none(*pgd)) {
		pte_dbg('!');
		return;
	}
	pte_dbg('1');

	/* For 2-level page tables, p4d/pud/pmd are folded */
	p4d = p4d_offset(pgd, vaddr);
	pud = pud_offset(p4d, vaddr);
	pmd = pmd_offset(pud, vaddr);

	if (pmd_none(*pmd)) {
		pte_dbg('?');
		return;
	}
	pte_dbg('2');

	/* Extract PTE table address from PMD */
	pte_table_phys = pmd_val(*pmd) & PTE_PFN_MASK;
	pte = (pte_t *)__va(pte_table_phys) + pte_index(vaddr);
	pte_dbg('3');

	if (!pte_none(pteval))
		*pte = pteval;
	else
		*pte = __pte(0);
	pte_dbg('4');

	/* Flush TLB for this address */
	asm volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
	pte_dbg(']');
}

int fixmaps_set;

void native_set_fixmap(unsigned /* enum fixed_addresses */ idx,
		       phys_addr_t phys, pgprot_t flags)
{
	unsigned long address = __fix_to_virt(idx);
	pte_t pte;

	if (idx >= __end_of_fixed_addresses) {
		pr_err("native_set_fixmap: idx %u out of range\n", idx);
		return;
	}

	/* Create the PTE */
	pte = pfn_pte(phys >> PAGE_SHIFT, flags);

	set_pte_vaddr(address, pte);
	fixmaps_set++;
}
EXPORT_SYMBOL(native_set_fixmap);

/* Page address lookup stub */
pte_t *lookup_address_in_pgd_attr(pgd_t *pgd, unsigned long address,
				  unsigned int *level, bool *nx, bool *rw)
{
	return NULL;
}
EXPORT_SYMBOL(lookup_address_in_pgd_attr);

/* vmalloc area stubs */
struct vm_struct *find_vm_area(const void *addr)
{
	return NULL;
}
EXPORT_SYMBOL(find_vm_area);

struct vm_struct *remove_vm_area(const void *addr)
{
	return NULL;
}
EXPORT_SYMBOL(remove_vm_area);

struct vm_struct *get_vm_area_caller(unsigned long size, unsigned long flags,
				     const void *caller)
{
	return NULL;
}
EXPORT_SYMBOL(get_vm_area_caller);

/* TLB state initialization - skip for NOMMU */
void initialize_tlbstate_and_flush(void)
{
	/* No TLB state to initialize without MMU */
}
EXPORT_SYMBOL(initialize_tlbstate_and_flush);

/* Text poking init - not needed for NOMMU (no isolated mm for patching) */
void __init poking_init(void)
{
	/* Without MMU, text patching uses direct memory access */
}

/* do_munmap stub */
int do_munmap(struct mm_struct *mm, unsigned long start, size_t len,
	      struct list_head *uf)
{
	return 0;
}
EXPORT_SYMBOL(do_munmap);

/* randomize_va_space */
int randomize_va_space;
EXPORT_SYMBOL(randomize_va_space);

/* ioremap_page_range stub */
int ioremap_page_range(unsigned long addr, unsigned long end,
		       phys_addr_t phys_addr, pgprot_t prot)
{
	return 0;
}
EXPORT_SYMBOL(ioremap_page_range);

/* free_vm_area stub */
void free_vm_area(struct vm_struct *area)
{
}
EXPORT_SYMBOL(free_vm_area);

/* vm_normal_page stub */
struct page *vm_normal_page(struct vm_area_struct *vma, unsigned long addr,
			    pte_t pte)
{
	return NULL;
}
EXPORT_SYMBOL(vm_normal_page);

/* zero_pfn - page frame number of the zero page */
unsigned long zero_pfn;
EXPORT_SYMBOL(zero_pfn);

/* insert_vm_struct stub */
int insert_vm_struct(struct mm_struct *mm, struct vm_area_struct *vma)
{
	return 0;
}
EXPORT_SYMBOL(insert_vm_struct);

#endif /* !CONFIG_MMU */
