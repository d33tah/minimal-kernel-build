/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_X86_CPU_ENTRY_AREA_H
#define _ASM_X86_CPU_ENTRY_AREA_H

#include <linux/percpu-defs.h>
#include <asm/processor.h>
#include <asm/intel_ds.h>
#include <asm/pgtable_areas.h>


struct doublefault_stack {
	unsigned long stack[(PAGE_SIZE - sizeof(struct x86_hw_tss)) / sizeof(unsigned long)];
	struct x86_hw_tss tss;
} __aligned(PAGE_SIZE);

/*
 * cpu_entry_area is a percpu region that contains things needed by the CPU
 * and early entry/exit code.  Real types aren't used for all fields here
 * to avoid circular header dependencies.
 *
 * Every field is a virtual alias of some other allocated backing store.
 * There is no direct allocation of a struct cpu_entry_area.
 */
struct cpu_entry_area {
	char gdt[PAGE_SIZE];

	/*
	 * The GDT is just below entry_stack and thus serves (on x86_64) as
	 * a read-only guard page. On 32-bit the GDT must be writeable, so
	 * it needs an extra guard page.
	 */
	char guard_entry_stack[PAGE_SIZE];
	struct entry_stack_page entry_stack_page;

	char guard_doublefault_stack[PAGE_SIZE];
	struct doublefault_stack doublefault_stack;

	/*
	 * On x86_64, the TSS is mapped RO.  On x86_32, it's mapped RW because
	 * we need task switches to work, and task switches write to the TSS.
	 */
	struct tss_struct tss;

	/*
	 * Per CPU debug store for Intel performance monitoring. Wastes a
	 * full page at the moment.
	 */
	struct debug_store cpu_debug_store;
	/*
	 * The actual PEBS/BTS buffers must be mapped to user space
	 * Reserve enough fixmap PTEs.
	 */
	struct debug_store_buffers cpu_debug_buffers;
};

#define CPU_ENTRY_AREA_SIZE		(sizeof(struct cpu_entry_area))
#define CPU_ENTRY_AREA_ARRAY_SIZE	(CPU_ENTRY_AREA_SIZE * NR_CPUS)

/* Total size includes the readonly IDT mapping page as well: */
#define CPU_ENTRY_AREA_TOTAL_SIZE	(CPU_ENTRY_AREA_ARRAY_SIZE + PAGE_SIZE)

DECLARE_PER_CPU(struct cpu_entry_area *, cpu_entry_area);
DECLARE_PER_CPU(struct cea_exception_stacks *, cea_exception_stacks);

extern void setup_cpu_entry_areas(void);
extern void cea_set_pte(void *cea_vaddr, phys_addr_t pa, pgprot_t flags);

extern struct cpu_entry_area *get_cpu_entry_area(int cpu);

static __always_inline struct entry_stack *cpu_entry_stack(int cpu)
{
	return &get_cpu_entry_area(cpu)->entry_stack_page.stack;
}

#define __this_cpu_ist_top_va(name)					\
	CEA_ESTACK_TOP(__this_cpu_read(cea_exception_stacks), name)

#define __this_cpu_ist_bottom_va(name)					\
	CEA_ESTACK_BOT(__this_cpu_read(cea_exception_stacks), name)

#endif
