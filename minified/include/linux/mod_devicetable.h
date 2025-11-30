/* Minimal mod_devicetable.h - reduced for minimal kernel */
#ifndef LINUX_MOD_DEVICETABLE_H
#define LINUX_MOD_DEVICETABLE_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/uuid.h>
typedef unsigned long kernel_ulong_t;
#endif

/* PCI */
#define PCI_ANY_ID (~0)

enum { PCI_ID_F_VFIO_DRIVER_OVERRIDE = 1, };

struct pci_device_id {
	__u32 vendor, device;
	__u32 subvendor, subdevice;
	__u32 class, class_mask;
	kernel_ulong_t driver_data;
	__u32 override_only;
};

/* Platform */
#define PLATFORM_NAME_SIZE	20
#define PLATFORM_MODULE_PREFIX	"platform:"

struct platform_device_id {
	char name[PLATFORM_NAME_SIZE];
	kernel_ulong_t driver_data;
};

/* ACPI */
#define ACPI_ID_LEN	16

struct acpi_device_id {
	__u8 id[ACPI_ID_LEN];
	kernel_ulong_t driver_data;
	__u32 cls;
	__u32 cls_msk;
};

/* DMI */
enum dmi_field {
	DMI_NONE, DMI_BIOS_VENDOR, DMI_BIOS_VERSION, DMI_BIOS_DATE,
	DMI_BIOS_RELEASE, DMI_EC_FIRMWARE_RELEASE, DMI_SYS_VENDOR,
	DMI_PRODUCT_NAME, DMI_PRODUCT_VERSION, DMI_PRODUCT_SERIAL,
	DMI_PRODUCT_UUID, DMI_PRODUCT_SKU, DMI_PRODUCT_FAMILY,
	DMI_BOARD_VENDOR, DMI_BOARD_NAME, DMI_BOARD_VERSION,
	DMI_BOARD_SERIAL, DMI_BOARD_ASSET_TAG, DMI_CHASSIS_VENDOR,
	DMI_CHASSIS_TYPE, DMI_CHASSIS_VERSION, DMI_CHASSIS_SERIAL,
	DMI_CHASSIS_ASSET_TAG, DMI_STRING_MAX, DMI_OEM_STRING,
};

struct dmi_strmatch {
	unsigned char slot:7;
	unsigned char exact_match:1;
	char substr[79];
};

struct dmi_system_id {
	int (*callback)(const struct dmi_system_id *);
	const char *ident;
	struct dmi_strmatch matches[4];
	void *driver_data;
};

#define dmi_device_id dmi_system_id
#define DMI_MATCH(a, b)	{ .slot = a, .substr = b }
#define DMI_EXACT_MATCH(a, b)	{ .slot = a, .substr = b, .exact_match = 1 }

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
