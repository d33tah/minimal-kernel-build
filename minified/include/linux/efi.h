/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_EFI_H
#define _LINUX_EFI_H

/*
 * Minimal EFI stub header - CONFIG_EFI is disabled
 * All EFI functionality is stubbed out to eliminate dead code
 */

#include <linux/init.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/screen_info.h>
#include <linux/reboot.h>
#include <asm/page.h>

/* Basic EFI types */
typedef unsigned long efi_status_t;
typedef u8 efi_bool_t;
typedef u16 efi_char16_t;
typedef u64 efi_physical_addr_t;
typedef void *efi_handle_t;
typedef guid_t efi_guid_t __aligned(__alignof__(u32));

/* EFI status codes */
#define EFI_SUCCESS		0
#define EFI_LOAD_ERROR		( 1 | (1UL << (BITS_PER_LONG-1)))
#define EFI_INVALID_PARAMETER	( 2 | (1UL << (BITS_PER_LONG-1)))
#define EFI_UNSUPPORTED		( 3 | (1UL << (BITS_PER_LONG-1)))
#define EFI_NOT_FOUND		(14 | (1UL << (BITS_PER_LONG-1)))

/* EFI memory types - needed by setup.c */
#define EFI_RESERVED_TYPE		 0
#define EFI_LOADER_CODE			 1
#define EFI_LOADER_DATA			 2
#define EFI_BOOT_SERVICES_CODE		 3
#define EFI_BOOT_SERVICES_DATA		 4
#define EFI_RUNTIME_SERVICES_CODE	 5
#define EFI_RUNTIME_SERVICES_DATA	 6
#define EFI_CONVENTIONAL_MEMORY		 7
#define EFI_UNUSABLE_MEMORY		 8
#define EFI_ACPI_RECLAIM_MEMORY		 9
#define EFI_ACPI_MEMORY_NVS		10
#define EFI_MEMORY_MAPPED_IO		11
#define EFI_MEMORY_MAPPED_IO_PORT_SPACE	12
#define EFI_PAL_CODE			13
#define EFI_PERSISTENT_MEMORY		14
#define EFI_MAX_MEMORY_TYPE		15

/* EFI memory attributes */
#define EFI_MEMORY_RUNTIME		(1UL << 63)

/* Memory descriptor */
typedef struct {
	u32 type;
	u32 pad;
	u64 phys_addr;
	u64 virt_addr;
	u64 num_pages;
	u64 attribute;
} efi_memory_desc_t;

/* EFI feature flags for efi_enabled() */
#define EFI_BOOT		0
#define EFI_CONFIG_TABLES	2
#define EFI_RUNTIME_SERVICES	3
#define EFI_MEMMAP		4
#define EFI_64BIT		5
#define EFI_PARAVIRT		6
#define EFI_ARCH_1		7
#define EFI_DBG			8
#define EFI_NX_PE_DATA		9
#define EFI_MEM_ATTR		10
#define EFI_MEM_NO_SOFT_RESERVE	11
#define EFI_PRESERVE_BS_REGIONS	12

/* Secure boot modes */
enum efi_secureboot_mode {
	efi_secureboot_mode_unset,
	efi_secureboot_mode_unknown,
	efi_secureboot_mode_disabled,
	efi_secureboot_mode_enabled,
};

/* Minimal runtime services structure - only used for offsetof in asm-offsets_32.c */
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

/* Stub functions - all return false/NULL/do nothing since CONFIG_EFI is disabled */
static inline bool efi_enabled(int feature)
{
	return false;
}

static inline void efi_init(void) {}
static inline void efi_enter_virtual_mode(void) {}
static inline void efi_fake_memmap(void) {}
static inline void efi_find_mirror(void) {}
static inline void efi_esrt_init(void) {}
static inline void efi_mokvar_table_init(void) {}
static inline void efi_reserve_boot_services(void) {}
static inline void efi_free_boot_services(void) {}
static inline int efi_memblock_x86_reserve_range(void) { return 0; }

static inline int efi_mem_type(unsigned long phys_addr)
{
	return 0;
}

static inline u64 efi_mem_attributes(unsigned long phys_addr)
{
	return 0;
}

static inline bool efi_soft_reserve_enabled(void)
{
	return false;
}

static inline void
efi_reboot(enum reboot_mode reboot_mode, const char *__unused) {}

static inline bool efi_poweroff_required(void)
{
	return false;
}

static inline int efi_status_to_err(efi_status_t status)
{
	return -EIO;
}

static inline bool efi_capsule_pending(int *reset_type)
{
	return false;
}

static inline void efi_crash_gracefully_on_page_fault(unsigned long phys_addr) {}

/* Stub for parse_efi_setup - used in setup.c */
static inline void parse_efi_setup(u64 phys_addr, u32 data_len) {}

/* Stub for efifb_setup_from_dmi */
static inline void efifb_setup_from_dmi(struct screen_info *si, const char *opt) {}

#endif /* _LINUX_EFI_H */
