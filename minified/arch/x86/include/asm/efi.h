/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_EFI_H
#define _ASM_X86_EFI_H

/*
 * Minimal EFI stub for x86 - CONFIG_EFI is disabled
 */

#include <linux/build_bug.h>
#include <linux/kernel.h>
#include <asm/tlbflush.h>

/* Stubs - all declarations moved to linux/efi.h to avoid conflicts */
extern unsigned long efi_fw_vendor, efi_config_table;
extern unsigned long efi_mixed_mode_stack_pa;

#define EFI32_LOADER_SIGNATURE	"EL32"
#define EFI64_LOADER_SIGNATURE	"EL64"

#define ARCH_EFI_IRQ_FLAGS_MASK	X86_EFLAGS_IF

/* kexec external ABI */
struct efi_setup_data {
	u64 fw_vendor;
	u64 __unused;
	u64 tables;
	u64 smbios;
	u64 reserved[8];
};

extern u64 efi_setup;

static inline bool efi_reboot_required(void)
{
	return false;
}

static inline bool efi_is_table_address(unsigned long phys_addr)
{
	return false;
}

static inline void efi_fake_memmap_early(void)
{
}

#define arch_ima_efi_boot_mode	\
	({ extern struct boot_params boot_params; boot_params.secure_boot; })

#endif /* _ASM_X86_EFI_H */
