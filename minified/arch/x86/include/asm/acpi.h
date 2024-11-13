/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_X86_ACPI_H
#define _ASM_X86_ACPI_H

/*
 *  Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2001 Patrick Mochel <mochel@osdl.org>
 */
#include <acpi/pdc_intel.h>

#include <asm/numa.h>
#include <asm/fixmap.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/mpspec.h>
#include <asm/x86_init.h>

#ifdef CONFIG_ACPI_APEI
# include <asm/pgtable_types.h>
#endif


#define acpi_lapic 0
#define acpi_ioapic 0
#define acpi_disable_cmcff 0
static inline void acpi_noirq_set(void) { }
static inline void acpi_disable_pci(void) { }
static inline void disable_acpi(void) { }

static inline void acpi_generic_reduced_hw_init(void) { }

static inline void x86_default_set_root_pointer(u64 addr) { }

static inline u64 x86_default_get_root_pointer(void)
{
	return 0;
}


#define ARCH_HAS_POWER_INIT	1

#ifdef CONFIG_ACPI_NUMA
extern int x86_acpi_numa_init(void);
#endif /* CONFIG_ACPI_NUMA */

struct cper_ia_proc_ctx;

#ifdef CONFIG_ACPI_APEI
static inline pgprot_t arch_apei_get_mem_attribute(phys_addr_t addr)
{
	/*
	 * We currently have no way to look up the EFI memory map
	 * attributes for a region in a consistent way, because the
	 * memmap is discarded after efi_free_boot_services(). So if
	 * you call efi_mem_attributes() during boot and at runtime,
	 * you could theoretically see different attributes.
	 *
	 * We are yet to see any x86 platforms that require anything
	 * other than PAGE_KERNEL (some ARM64 platforms require the
	 * equivalent of PAGE_KERNEL_NOCACHE). Additionally, if SME
	 * is active, the ACPI information will not be encrypted,
	 * so return PAGE_KERNEL_NOENC until we know differently.
	 */
	return PAGE_KERNEL_NOENC;
}

int arch_apei_report_x86_error(struct cper_ia_proc_ctx *ctx_info,
			       u64 lapic_id);
#else
static inline int arch_apei_report_x86_error(struct cper_ia_proc_ctx *ctx_info,
					     u64 lapic_id)
{
	return -EINVAL;
}
#endif

#define ACPI_TABLE_UPGRADE_MAX_PHYS (max_low_pfn_mapped << PAGE_SHIFT)

#endif /* _ASM_X86_ACPI_H */
