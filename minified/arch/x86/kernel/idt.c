#include <linux/interrupt.h>

#include <asm/hw_irq.h>
#include <asm/idtentry.h>

#define DPL0 0x0
#define DPL3 0x3

#define DEFAULT_STACK 0

#define G(_vector, _addr, _ist, _type, _dpl, _segment) \
	{                                              \
		.vector = _vector,                     \
		.bits.ist = _ist,                      \
		.bits.type = _type,                    \
		.bits.dpl = _dpl,                      \
		.bits.p = 1,                           \
		.addr = _addr,                         \
		.segment = _segment,                   \
	}

#define INTG(_vector, _addr) \
	G(_vector, _addr, DEFAULT_STACK, GATE_INTERRUPT, DPL0, __KERNEL_CS)

#define SYSG(_vector, _addr) \
	G(_vector, _addr, DEFAULT_STACK, GATE_INTERRUPT, DPL3, __KERNEL_CS)

#define ISTG(_vector, _addr, _ist) INTG(_vector, _addr)

#define TSKG(_vector, _gdt) \
	G(_vector, NULL, DEFAULT_STACK, GATE_TASK, DPL0, _gdt << 3)

#define IDT_TABLE_SIZE (IDT_ENTRIES * sizeof(gate_desc))

static const __initconst struct idt_data early_idts[] = {
	INTG(X86_TRAP_DB, asm_exc_debug),
	SYSG(X86_TRAP_BP, asm_exc_int3),

	INTG(X86_TRAP_PF, asm_exc_page_fault),
};

static const __initconst struct idt_data def_idts[] = {
	INTG(X86_TRAP_DE, asm_exc_divide_error),
	ISTG(X86_TRAP_NMI, asm_exc_nmi, IST_INDEX_NMI),
	INTG(X86_TRAP_BR, asm_exc_bounds),
	INTG(X86_TRAP_UD, asm_exc_invalid_op),
	INTG(X86_TRAP_NM, asm_exc_device_not_available),
	INTG(X86_TRAP_OLD_MF, asm_exc_coproc_segment_overrun),
	INTG(X86_TRAP_TS, asm_exc_invalid_tss),
	INTG(X86_TRAP_NP, asm_exc_segment_not_present),
	INTG(X86_TRAP_SS, asm_exc_stack_segment),
	INTG(X86_TRAP_GP, asm_exc_general_protection),
	INTG(X86_TRAP_SPURIOUS, asm_exc_spurious_interrupt_bug),
	INTG(X86_TRAP_MF, asm_exc_coprocessor_error),
	INTG(X86_TRAP_AC, asm_exc_alignment_check),
	INTG(X86_TRAP_XF, asm_exc_simd_coprocessor_error),

	TSKG(X86_TRAP_DF, GDT_ENTRY_DOUBLEFAULT_TSS),
	ISTG(X86_TRAP_DB, asm_exc_debug, IST_INDEX_DB),

	SYSG(X86_TRAP_OF, asm_exc_overflow),
	SYSG(IA32_SYSCALL_VECTOR, entry_INT80_32),
};

static gate_desc idt_table[IDT_ENTRIES] __page_aligned_bss;

static struct desc_ptr idt_descr __ro_after_init = {
	.size = IDT_TABLE_SIZE - 1,
	.address = (unsigned long)idt_table,
};

void load_current_idt(void)
{
	load_idt(&idt_descr);
}

static __init void idt_setup_from_table(gate_desc *idt,
					const struct idt_data *t, int size,
					bool sys)
{
	gate_desc desc;

	for (; size > 0; t++, size--) {
		idt_init_desc(&desc, t);
		write_idt_entry(idt, t->vector, &desc);
		if (sys)
			set_bit(t->vector, system_vectors);
	}
}

static __init void set_intr_gate(unsigned int n, const void *addr)
{
	struct idt_data data;

	BUG_ON(n > 0xFF);
	memset(&data, 0, sizeof(data));
	data.vector = n;
	data.addr = addr;
	data.segment = __KERNEL_CS;
	data.bits.type = GATE_INTERRUPT;
	data.bits.p = 1;

	idt_setup_from_table(idt_table, &data, 1, false);
}

void __init idt_setup_early_traps(void)
{
	idt_setup_from_table(idt_table, early_idts, ARRAY_SIZE(early_idts),
			     true);
	load_idt(&idt_descr);
}

void __init idt_setup_traps(void)
{
	idt_setup_from_table(idt_table, def_idts, ARRAY_SIZE(def_idts), true);
}

void __init idt_setup_apic_and_irq_gates(void)
{
	int i = FIRST_EXTERNAL_VECTOR;
	void *entry;

	for_each_clear_bit_from(i, system_vectors, FIRST_SYSTEM_VECTOR) {
		entry = irq_entries_start +
			IDT_ALIGN * (i - FIRST_EXTERNAL_VECTOR);
		set_intr_gate(i, entry);
	}

	cea_set_pte(CPU_ENTRY_AREA_RO_IDT_VADDR, __pa_symbol(idt_table),
		    PAGE_KERNEL_RO);
	idt_descr.address = CPU_ENTRY_AREA_RO_IDT;
	load_idt(&idt_descr);
}

void __init idt_setup_early_handler(void)
{
	int i;

	for (i = 0; i < NUM_EXCEPTION_VECTORS; i++)
		set_intr_gate(i, early_idt_handler_array[i]);
	for (; i < NR_VECTORS; i++)
		set_intr_gate(i, early_ignore_irq);
	load_idt(&idt_descr);
}
