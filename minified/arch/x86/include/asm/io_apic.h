/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IO_APIC_H
#define _ASM_X86_IO_APIC_H

#include <linux/types.h>
#include <asm/mpspec.h>
#include <asm/apicdef.h>
#include <asm/irq_vectors.h>
#include <asm/x86_init.h>
/*
 * Intel IO-APIC support for SMP and UP systems.
 *
 * Copyright (C) 1997, 1998, 1999, 2000 Ingo Molnar
 */

/*
 * The structure of the IO-APIC:
 */
union IO_APIC_reg_00 {
	u32	raw;
	struct {
		u32	__reserved_2	: 14,
			LTS		:  1,
			delivery_type	:  1,
			__reserved_1	:  8,
			ID		:  8;
	} __attribute__ ((packed)) bits;
};

union IO_APIC_reg_01 {
	u32	raw;
	struct {
		u32	version		:  8,
			__reserved_2	:  7,
			PRQ		:  1,
			entries		:  8,
			__reserved_1	:  8;
	} __attribute__ ((packed)) bits;
};

union IO_APIC_reg_02 {
	u32	raw;
	struct {
		u32	__reserved_2	: 24,
			arbitration	:  4,
			__reserved_1	:  4;
	} __attribute__ ((packed)) bits;
};

union IO_APIC_reg_03 {
	u32	raw;
	struct {
		u32	boot_DT		:  1,
			__reserved_1	: 31;
	} __attribute__ ((packed)) bits;
};

struct IO_APIC_route_entry {
	union {
		struct {
			u64	vector			:  8,
				delivery_mode		:  3,
				dest_mode_logical	:  1,
				delivery_status		:  1,
				active_low		:  1,
				irr			:  1,
				is_level		:  1,
				masked			:  1,
				reserved_0		: 15,
				reserved_1		: 17,
				virt_destid_8_14	:  7,
				destid_0_7		:  8;
		};
		struct {
			u64	ir_shared_0		:  8,
				ir_zero			:  3,
				ir_index_15		:  1,
				ir_shared_1		:  5,
				ir_reserved_0		: 31,
				ir_format		:  1,
				ir_index_0_14		: 15;
		};
		struct {
			u64	w1			: 32,
				w2			: 32;
		};
	};
} __attribute__ ((packed));

struct irq_alloc_info;
struct ioapic_domain_cfg;

#define	IOAPIC_MAP_ALLOC		0x1
#define	IOAPIC_MAP_CHECK		0x2


#define IO_APIC_IRQ(x)		0
#define io_apic_assign_pci_irqs 0
#define setup_ioapic_ids_from_mpc x86_init_noop
static inline void ioapic_insert_resources(void) { }
static inline int arch_early_ioapic_init(void) { return 0; }
static inline void print_IO_APICs(void) {}
#define gsi_top (NR_IRQS_LEGACY)
static inline int mp_find_ioapic(u32 gsi) { return 0; }
static inline int mp_map_gsi_to_irq(u32 gsi, unsigned int flags,
				    struct irq_alloc_info *info)
{
	return gsi;
}

static inline void mp_unmap_irq(int irq) { }

static inline int save_ioapic_entries(void)
{
	return -ENOMEM;
}

static inline void mask_ioapic_entries(void) { }
static inline int restore_ioapic_entries(void)
{
	return -ENOMEM;
}

static inline void mp_save_irq(struct mpc_intsrc *m) { }
static inline void disable_ioapic_support(void) { }
static inline void io_apic_init_mappings(void) { }
#define native_io_apic_read		NULL
#define native_restore_boot_irq_mode	NULL

static inline void setup_IO_APIC(void) { }
static inline void enable_IO_APIC(void) { }
static inline void restore_boot_irq_mode(void) { }


#endif /* _ASM_X86_IO_APIC_H */
