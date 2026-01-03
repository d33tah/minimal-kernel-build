/* Minimal mod_devicetable.h - reduced for minimal kernel */
#ifndef LINUX_MOD_DEVICETABLE_H
#define LINUX_MOD_DEVICETABLE_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/uuid.h>
typedef unsigned long kernel_ulong_t;
#endif

/* PCI, ACPI, DMI structures removed - unused in minimal kernel */

/* Platform */
#define PLATFORM_NAME_SIZE	20
#define PLATFORM_MODULE_PREFIX	"platform:"

struct platform_device_id {
	char name[PLATFORM_NAME_SIZE];
	kernel_ulong_t driver_data;
};

/* OF (Device Tree) */
struct of_device_id {
	char name[32];
	char type[32];
	char compatible[128];
	const void *data;
};

/* x86 CPU */
#define x86cpu_device_id x86_cpu_id
struct x86_cpu_id {
	__u16 vendor;
	__u16 family;
	__u16 model;
	__u16 steppings;
	__u16 feature;
	kernel_ulong_t driver_data;
};

#define X86_VENDOR_ANY 0xffff
#define X86_FAMILY_ANY 0
#define X86_MODEL_ANY  0
#define X86_STEPPING_ANY 0
#define X86_FEATURE_ANY 0

struct cpu_feature {
	__u16 feature;
};

#endif /* LINUX_MOD_DEVICETABLE_H */
