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


struct cper_ia_proc_ctx;

static inline int arch_apei_report_x86_error(struct cper_ia_proc_ctx *ctx_info,
					     u64 lapic_id)
{
	return -EINVAL;
}

#define ACPI_TABLE_UPGRADE_MAX_PHYS (max_low_pfn_mapped << PAGE_SHIFT)

#endif /* _ASM_X86_ACPI_H */
