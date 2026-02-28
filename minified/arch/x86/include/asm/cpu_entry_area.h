 
#ifndef _ASM_X86_CPU_ENTRY_AREA_H
#define _ASM_X86_CPU_ENTRY_AREA_H

#include <linux/percpu-defs.h>
#include <asm/processor.h>
#include <asm/pgtable_areas.h>

struct doublefault_stack {
	unsigned long stack[(PAGE_SIZE - sizeof(struct x86_hw_tss)) / sizeof(unsigned long)];
	struct x86_hw_tss tss;
} __aligned(PAGE_SIZE);

struct cpu_entry_area {
	char gdt[PAGE_SIZE];

	char guard_entry_stack[PAGE_SIZE];
	struct entry_stack_page entry_stack_page;

	char guard_doublefault_stack[PAGE_SIZE];
	struct doublefault_stack doublefault_stack;

	struct tss_struct tss;
};

#define CPU_ENTRY_AREA_SIZE		(sizeof(struct cpu_entry_area))
#define CPU_ENTRY_AREA_ARRAY_SIZE	(CPU_ENTRY_AREA_SIZE * NR_CPUS)

DECLARE_PER_CPU(struct cpu_entry_area *, cpu_entry_area);

extern void setup_cpu_entry_areas(void);
extern void cea_set_pte(void *cea_vaddr, phys_addr_t pa, pgprot_t flags);

extern struct cpu_entry_area *get_cpu_entry_area(int cpu);

static __always_inline struct entry_stack *cpu_entry_stack(int cpu)
{
	return &get_cpu_entry_area(cpu)->entry_stack_page.stack;
}

#endif
