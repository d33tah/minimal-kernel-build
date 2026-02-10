 
#ifndef _ASM_X86_DESC_H
#define _ASM_X86_DESC_H

#include <asm/desc_defs.h>
#ifndef LDT_ENTRY_SIZE
#define LDT_ENTRY_SIZE 8 /* inlined from asm/ldt.h */
#endif
#include <asm/mmu.h>
#include <asm/fixmap.h>
#include <asm/irq_vectors.h>
#include <asm/cpu_entry_area.h>

#include <linux/debug_locks.h>
#include <linux/smp.h>
#include <linux/percpu.h>

/* fill_ldt removed - never called */

struct gdt_page {
	struct desc_struct gdt[GDT_ENTRIES];
} __attribute__((aligned(PAGE_SIZE)));

DECLARE_PER_CPU_PAGE_ALIGNED(struct gdt_page, gdt_page);

 
static inline struct desc_struct *get_cpu_gdt_rw(unsigned int cpu)
{
	return per_cpu(gdt_page, cpu).gdt;
}

 
static inline struct desc_struct *get_current_gdt_rw(void)
{
	return this_cpu_ptr(&gdt_page)->gdt;
}

 
static inline struct desc_struct *get_cpu_gdt_ro(int cpu)
{
	return (struct desc_struct *)&get_cpu_entry_area(cpu)->gdt;
}

/* get_current_gdt_ro removed - unused */

static inline phys_addr_t get_cpu_gdt_paddr(unsigned int cpu)
{
	return per_cpu_ptr_to_phys(get_cpu_gdt_rw(cpu));
}

/* pack_gate removed - unused */

/* desc_empty removed - unused */

#define load_TR_desc()				native_load_tr_desc()
#define load_gdt(dtr)				native_load_gdt(dtr)
#define load_idt(dtr)				native_load_idt(dtr)
#define load_TLS(t, cpu)			native_load_tls(t, cpu)
#define set_ldt					native_set_ldt
#define write_gdt_entry(dt, entry, desc, type)	native_write_gdt_entry(dt, entry, desc, type)
#define write_idt_entry(dt, entry, g)		native_write_idt_entry(dt, entry, g)

static inline void native_write_idt_entry(gate_desc *idt, int entry, const gate_desc *gate)
{
	memcpy(&idt[entry], gate, sizeof(*gate));
}

static inline void
native_write_gdt_entry(struct desc_struct *gdt, int entry, const void *desc, int type)
{
	unsigned int size;

	switch (type) {
	case DESC_TSS:	size = sizeof(tss_desc);	break;
	case DESC_LDT:	size = sizeof(ldt_desc);	break;
	default:	size = sizeof(*gdt);		break;
	}

	memcpy(&gdt[entry], desc, size);
}

static inline void set_tssldt_descriptor(void *d, unsigned long addr,
					 unsigned type, unsigned size)
{
	struct ldttss_desc *desc = d;

	memset(desc, 0, sizeof(*desc));

	desc->limit0		= (u16) size;
	desc->base0		= (u16) addr;
	desc->base1		= (addr >> 16) & 0xFF;
	desc->type		= type;
	desc->p			= 1;
	desc->limit1		= (size >> 16) & 0xF;
	desc->base2		= (addr >> 24) & 0xFF;
}

static inline void __set_tss_desc(unsigned cpu, unsigned int entry, struct x86_hw_tss *addr)
{
	struct desc_struct *d = get_cpu_gdt_rw(cpu);
	tss_desc tss;

	set_tssldt_descriptor(&tss, (unsigned long)addr, DESC_TSS,
			      __KERNEL_TSS_LIMIT);
	write_gdt_entry(d, entry, &tss, DESC_TSS);
}

#define set_tss_desc(cpu, addr) __set_tss_desc(cpu, GDT_ENTRY_TSS, addr)

static inline void native_set_ldt(const void *addr, unsigned int entries)
{
	if (likely(entries == 0))
		asm volatile("lldt %w0"::"q" (0));
	else {
		unsigned cpu = smp_processor_id();
		ldt_desc ldt;

		set_tssldt_descriptor(&ldt, (unsigned long)addr, DESC_LDT,
				      entries * LDT_ENTRY_SIZE - 1);
		write_gdt_entry(get_cpu_gdt_rw(cpu), GDT_ENTRY_LDT,
				&ldt, DESC_LDT);
		asm volatile("lldt %w0"::"q" (GDT_ENTRY_LDT*8));
	}
}

static inline void native_load_gdt(const struct desc_ptr *dtr)
{
	asm volatile("lgdt %0"::"m" (*dtr));
}

static __always_inline void native_load_idt(const struct desc_ptr *dtr)
{
	asm volatile("lidt %0"::"m" (*dtr));
}

/* native_store_gdt, store_idt, native_store_tr removed - unused */

static inline void native_load_tr_desc(void)
{
	asm volatile("ltr %w0"::"q" (GDT_ENTRY_TSS*8));
}

static inline void native_load_tls(struct thread_struct *t, unsigned int cpu)
{
	struct desc_struct *gdt = get_cpu_gdt_rw(cpu);
	unsigned int i;

	for (i = 0; i < GDT_ENTRY_TLS_ENTRIES; i++)
		gdt[GDT_ENTRY_TLS_MIN + i] = t->tls_array[i];
}

/* DECLARE_PER_CPU(bool, __tss_limit_invalid) removed - write-only variable */

/* force_reload_TR inlined at arch/x86/kernel/doublefault_32.c - single caller */

/* refresh_tss_limit, invalidate_tss_limit removed - unused */
/* get_desc_base, set_desc_base, get_desc_limit, set_desc_limit removed - unused */

static inline void clear_LDT(void)
{
	set_ldt(NULL, 0);
}

/* alloc_intr_gate removed - never called */
/* init_idt_data inlined at arch/x86/kernel/idt.c - single caller */

static inline void idt_init_desc(gate_desc *gate, const struct idt_data *d)
{
	unsigned long addr = (unsigned long) d->addr;

	gate->offset_low	= (u16) addr;
	gate->segment		= (u16) d->segment;
	gate->bits		= d->bits;
	gate->offset_middle	= (u16) (addr >> 16);
}

extern unsigned long system_vectors[];

extern void load_current_idt(void);
extern void idt_setup_early_handler(void);
extern void idt_setup_early_traps(void);
extern void idt_setup_traps(void);
extern void idt_setup_apic_and_irq_gates(void);
/* idt_is_f00f_address, idt_setup_early_pf, idt_invalidate removed - unused */

#endif  
