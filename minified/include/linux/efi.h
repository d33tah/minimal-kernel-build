/* --- 2025-12-08 04:18 --- Minimal efi.h - reduced unused typedefs */
#ifndef _LINUX_EFI_H
#define _LINUX_EFI_H

#include <linux/init.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/screen_info.h>
#include <asm/page.h>

#define EFI_BOOT_SERVICES_DATA		 4
#define EFI_RUNTIME_SERVICES_DATA	 6
#define EFI_CONVENTIONAL_MEMORY		 7

#define EFI_MEMORY_RUNTIME		(1UL << 63)

typedef struct {
	u32 type;
	u32 pad;
	u64 phys_addr;
	u64 virt_addr;
	u64 num_pages;
	u64 attribute;
} efi_memory_desc_t;

#define EFI_BOOT		0
#define EFI_RUNTIME_SERVICES	3

typedef struct {
	u32 get_time;
	u32 set_time;
	u32 get_wakeup_time;
	u32 set_wakeup_time;
	u32 set_virtual_address_map;
	u32 convert_pointer;
	u32 get_variable;
	u32 get_next_variable;
	u32 set_variable;
	u32 get_next_high_mono_count;
	u32 reset_system;
	u32 update_capsule;
	u32 query_capsule_caps;
	u32 query_variable_info;
} efi_runtime_services_32_t;

typedef efi_runtime_services_32_t efi_runtime_services_t;

#endif
