/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_DESC_H
#define _ASM_X86_DESC_H

#include <linux/mutex.h>
#include <asm/fixmap.h>
#include <asm/irq_vectors.h>

struct gdt_page {
	struct desc_struct gdt[GDT_ENTRIES];
} __attribute__((aligned(PAGE_SIZE)));

DECLARE_PER_CPU_PAGE_ALIGNED(struct gdt_page, gdt_page);

/* Provide the original GDT */
static inline struct desc_struct *get_cpu_gdt_rw(unsigned int cpu)
{
	return per_cpu(gdt_page, cpu).gdt;
}

/* Provide the current original GDT */
static inline struct desc_struct *get_current_gdt_rw(void)
{
	return this_cpu_ptr(&gdt_page)->gdt;
}

#define load_TR_desc() native_load_tr_desc()
#define load_gdt(dtr) native_load_gdt(dtr)
#define load_idt(dtr) native_load_idt(dtr)
#define write_gdt_entry(dt, entry, desc, type) \
	native_write_gdt_entry(dt, entry, desc, type)
#define write_idt_entry(dt, entry, g) native_write_idt_entry(dt, entry, g)

static inline void native_write_idt_entry(gate_desc *idt, int entry,
					  const gate_desc *gate)
{
	memcpy(&idt[entry], gate, sizeof(*gate));
}

static inline void native_write_gdt_entry(struct desc_struct *gdt, int entry,
					  const void *desc, int type)
{
	unsigned int size;

	switch (type) {
	case DESC_TSS:
		size = sizeof(tss_desc);
		break;
	default:
		size = sizeof(*gdt);
		break;
	}

	memcpy(&gdt[entry], desc, size);
}

static inline void set_tssldt_descriptor(void *d, unsigned long addr,
					 unsigned type, unsigned size)
{
	struct ldttss_desc *desc = d;

	memset(desc, 0, sizeof(*desc));

	desc->limit0 = (u16)size;
	desc->base0 = (u16)addr;
	desc->base1 = (addr >> 16) & 0xFF;
	desc->type = type;
	desc->p = 1;
	desc->limit1 = (size >> 16) & 0xF;
	desc->base2 = (addr >> 24) & 0xFF;
}

static inline void __set_tss_desc(unsigned cpu, unsigned int entry,
				  struct x86_hw_tss *addr)
{
	struct desc_struct *d = get_cpu_gdt_rw(cpu);
	tss_desc tss;

	set_tssldt_descriptor(&tss, (unsigned long)addr, DESC_TSS,
			      __KERNEL_TSS_LIMIT);
	write_gdt_entry(d, entry, &tss, DESC_TSS);
}

#define set_tss_desc(cpu, addr) __set_tss_desc(cpu, GDT_ENTRY_TSS, addr)

static inline void native_load_gdt(const struct desc_ptr *dtr)
{
	asm volatile("lgdt %0" ::"m"(*dtr));
}

static __always_inline void native_load_idt(const struct desc_ptr *dtr)
{
	asm volatile("lidt %0" ::"m"(*dtr));
}

static inline void native_load_tr_desc(void)
{
	asm volatile("ltr %w0" ::"q"(GDT_ENTRY_TSS * 8));
}

static inline void force_reload_TR(void)
{
	struct desc_struct *d = get_current_gdt_rw();
	tss_desc tss;

	memcpy(&tss, &d[GDT_ENTRY_TSS], sizeof(tss_desc));

	/*
	 * LTR requires an available TSS, and the TSS is currently
	 * busy.  Make it be available so that LTR will work.
	 */
	tss.type = DESC_TSS;
	write_gdt_entry(d, GDT_ENTRY_TSS, &tss, DESC_TSS);

	load_TR_desc();
}

static inline void init_idt_data(struct idt_data *data, unsigned int n,
				 const void *addr)
{
	BUG_ON(n > 0xFF);

	memset(data, 0, sizeof(*data));
	data->vector = n;
	data->addr = addr;
	data->segment = __KERNEL_CS;
	data->bits.type = GATE_INTERRUPT;
	data->bits.p = 1;
}

static inline void idt_init_desc(gate_desc *gate, const struct idt_data *d)
{
	unsigned long addr = (unsigned long)d->addr;

	gate->offset_low = (u16)addr;
	gate->segment = (u16)d->segment;
	gate->bits = d->bits;
	gate->offset_middle = (u16)(addr >> 16);
}

extern unsigned long system_vectors[];

extern void load_current_idt(void);
extern void idt_setup_early_handler(void);
extern void idt_setup_early_traps(void);
extern void idt_setup_traps(void);
extern void idt_setup_apic_and_irq_gates(void);

#endif
