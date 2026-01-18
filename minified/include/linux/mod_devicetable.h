/* Minimal mod_devicetable.h - reduced for minimal kernel */
#ifndef LINUX_MOD_DEVICETABLE_H
#define LINUX_MOD_DEVICETABLE_H

#ifdef __KERNEL__
#include <linux/types.h>
/* uuid.h removed - guid_t not used */
typedef unsigned long kernel_ulong_t;
#endif

/* PCI, ACPI, DMI structures removed - unused in minimal kernel */

/* Platform */
#define PLATFORM_NAME_SIZE	20

struct platform_device_id {
	char name[PLATFORM_NAME_SIZE];
	kernel_ulong_t driver_data;
};

/* OF (Device Tree) of_device_id struct, x86 CPU x86_cpu_id struct and X86_MATCH macros removed - never used */

#endif /* LINUX_MOD_DEVICETABLE_H */
