#ifndef _ASM_X86_ACPI_H
#define _ASM_X86_ACPI_H
/* acpi_noirq, acpi_strict, acpi_disabled (was 1) removed */
#define acpi_ioapic 0

static inline void acpi_generic_reduced_hw_init(void) { }

static inline void x86_default_set_root_pointer(u64 addr) { }

static inline u64 x86_default_get_root_pointer(void)
{
	return 0;
}

#endif
