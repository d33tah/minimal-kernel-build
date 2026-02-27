

#include <linux/pgtable.h>

#include <asm/desc.h>

static DEFINE_PER_CPU_PAGE_ALIGNED(struct entry_stack_page,
				   entry_stack_storage);

DEFINE_PER_CPU_PAGE_ALIGNED(struct doublefault_stack, doublefault_stack) = {};

noinstr struct cpu_entry_area *get_cpu_entry_area(int cpu)
{
	unsigned long va = CPU_ENTRY_AREA_PER_CPU + cpu * CPU_ENTRY_AREA_SIZE;
	return (struct cpu_entry_area *)va;
}

void cea_set_pte(void *cea_vaddr, phys_addr_t pa, pgprot_t flags)
{
	unsigned long va = (unsigned long)cea_vaddr;
	pte_t pte = pfn_pte(pa >> PAGE_SHIFT, flags);

	if (boot_cpu_has(X86_FEATURE_PGE) &&
	    (pgprot_val(flags) & _PAGE_PRESENT))
		pte = pte_set_flags(pte, _PAGE_GLOBAL);

	set_pte_vaddr(va, pte);
}

static void __init cea_map_percpu_pages(void *cea_vaddr, void *ptr, int pages,
					pgprot_t prot)
{
	for (; pages; pages--, cea_vaddr += PAGE_SIZE, ptr += PAGE_SIZE)
		cea_set_pte(cea_vaddr, per_cpu_ptr_to_phys(ptr), prot);
}

void __init setup_cpu_entry_areas(void)
{
	unsigned long start, end;
	struct cpu_entry_area *cea;
	pgprot_t gdt_prot, tss_prot;

	start = CPU_ENTRY_AREA_BASE;
	end = start + CPU_ENTRY_AREA_MAP_SIZE;

	for (; start < end && start >= CPU_ENTRY_AREA_BASE; start += PMD_SIZE)
		populate_extra_pte(start);

	cea = get_cpu_entry_area(0);
	gdt_prot = boot_cpu_has(X86_FEATURE_XENPV) ? PAGE_KERNEL_RO :
						     PAGE_KERNEL;
	tss_prot = PAGE_KERNEL;

	cea_set_pte(&cea->gdt, get_cpu_gdt_paddr(0), gdt_prot);

	cea_map_percpu_pages(&cea->entry_stack_page,
			     per_cpu_ptr(&entry_stack_storage, 0), 1,
			     PAGE_KERNEL);

	cea_map_percpu_pages(&cea->tss, &per_cpu(cpu_tss_rw, 0),
			     sizeof(struct tss_struct) / PAGE_SIZE, tss_prot);

	per_cpu(cpu_entry_area, 0) = cea;

	cea_map_percpu_pages(&cea->doublefault_stack,
			     &per_cpu(doublefault_stack, 0), 1, PAGE_KERNEL);

	sync_initial_page_table();
}
