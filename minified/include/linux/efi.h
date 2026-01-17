/* --- 2025-12-08 04:18 --- Minimal efi.h - reduced unused typedefs */
#ifndef _LINUX_EFI_H
#define _LINUX_EFI_H

#include <linux/init.h>
#include <linux/types.h>
/* uuid.h removed - guid_t not used */
#include <linux/screen_info.h>
#include <asm/page.h>

/* EFI_CONVENTIONAL_MEMORY, efi_memory_desc_t, EFI_BOOT, EFI_RUNTIME_SERVICES,
   efi_secureboot_mode enum, efi_runtime_services_32_t, efi_runtime_services_t removed - never used */

static inline bool efi_enabled(int feature)
{
	return false;
}
/* efi_memblock_x86_reserve_range, efi_mem_type removed - unused */

#endif
