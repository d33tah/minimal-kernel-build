/* Minimal mod_devicetable.h - reduced for minimal kernel */
#ifndef LINUX_MOD_DEVICETABLE_H
#define LINUX_MOD_DEVICETABLE_H

#ifdef __KERNEL__
#include <linux/types.h>
/* uuid.h, kernel_ulong_t removed - unused */
#endif

/* PCI, ACPI, DMI structures removed - unused in minimal kernel */

/* Platform */
/* PLATFORM_NAME_SIZE macro and struct platform_device_id removed - never used */

/* OF (Device Tree) of_device_id struct, x86 CPU x86_cpu_id struct and X86_MATCH macros removed - never used */

#endif /* LINUX_MOD_DEVICETABLE_H */
