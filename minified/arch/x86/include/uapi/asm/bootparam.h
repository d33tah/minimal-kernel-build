/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_X86_BOOTPARAM_H
#define _ASM_X86_BOOTPARAM_H

#define SETUP_CC_BLOB 7

#define SETUP_INDIRECT (1 << 31)

/* SETUP_INDIRECT | max(SETUP_*) */
#define SETUP_TYPE_MAX (SETUP_INDIRECT | SETUP_CC_BLOB)

/* loadflags */
#define LOADED_HIGH (1 << 0)
#define CAN_USE_HEAP (1 << 7)

#ifndef __ASSEMBLY__

#include <linux/screen_info.h>
#include <linux/edd.h>

struct apm_bios_info {
	__u16 version;
	__u16 cseg;
	__u32 offset;
	__u16 cseg_16;
	__u16 dseg;
	__u16 flags;
	__u16 cseg_len;
	__u16 cseg_16_len;
	__u16 dseg_len;
};

struct ist_info {
	__u32 signature;
	__u32 command;
	__u32 event;
	__u32 perf_level;
};

struct edid_info {
	unsigned char dummy[128];
};

/* extensible setup data list node */
struct setup_data {
	__u64 next;
	__u32 type;
	__u32 len;
	__u8 data[0];
};

struct setup_header {
	__u8 setup_sects;
	__u16 root_flags;
	__u32 syssize;
	__u16 ram_size;
	__u16 vid_mode;
	__u16 root_dev;
	__u16 boot_flag;
	__u16 jump;
	__u32 header;
	__u16 version;
	__u32 realmode_swtch;
	__u16 start_sys_seg;
	__u16 kernel_version;
	__u8 type_of_loader;
	__u8 loadflags;
	__u16 setup_move_size;
	__u32 code32_start;
	__u32 ramdisk_image;
	__u32 ramdisk_size;
	__u32 bootsect_kludge;
	__u16 heap_end_ptr;
	__u8 ext_loader_ver;
	__u8 ext_loader_type;
	__u32 cmd_line_ptr;
	__u32 initrd_addr_max;
	__u32 kernel_alignment;
	__u8 relocatable_kernel;
	__u8 min_alignment;
	__u16 xloadflags;
	__u32 cmdline_size;
	__u32 hardware_subarch;
	__u64 hardware_subarch_data;
	__u32 payload_offset;
	__u32 payload_length;
	__u64 setup_data;
	__u64 pref_address;
	__u32 init_size;
	__u32 handover_offset;
	__u32 kernel_info_offset;
} __attribute__((packed));

struct sys_desc_table {
	__u16 length;
	__u8 table[14];
};

/* Gleaned from OFW's set-parameters in cpu/x86/pc/linux.fth */
struct olpc_ofw_header {
	__u32 ofw_magic; /* OFW signature */
	__u32 ofw_version;
	__u32 cif_handler; /* callback into OFW */
	__u32 irq_desc_table;
} __attribute__((packed));

struct efi_info {
	__u32 efi_loader_signature;
	__u32 efi_systab;
	__u32 efi_memdesc_size;
	__u32 efi_memdesc_version;
	__u32 efi_memmap;
	__u32 efi_memmap_size;
	__u32 efi_systab_hi;
	__u32 efi_memmap_hi;
};

/*
 * This is the maximum number of entries in struct boot_params::e820_table
 * (the zeropage), which is part of the x86 boot protocol ABI:
 */
#define E820_MAX_ENTRIES_ZEROPAGE 128

/*
 * The E820 memory region entry of the boot protocol ABI:
 */
struct boot_e820_entry {
	__u64 addr;
	__u64 size;
	__u32 type;
} __attribute__((packed));

/* The so-called "zeropage" */
struct boot_params {
	struct screen_info screen_info; /* 0x000 */
	struct apm_bios_info apm_bios_info; /* 0x040 */
	__u8 _pad2[4]; /* 0x054 */
	__u64 tboot_addr; /* 0x058 */
	struct ist_info ist_info; /* 0x060 */
	__u64 acpi_rsdp_addr; /* 0x070 */
	__u8 _pad3[8]; /* 0x078 */
	__u8 hd0_info[16]; /* obsolete! */ /* 0x080 */
	__u8 hd1_info[16]; /* obsolete! */ /* 0x090 */
	struct sys_desc_table sys_desc_table; /* obsolete! */ /* 0x0a0 */
	struct olpc_ofw_header olpc_ofw_header; /* 0x0b0 */
	__u32 ext_ramdisk_image; /* 0x0c0 */
	__u32 ext_ramdisk_size; /* 0x0c4 */
	__u32 ext_cmd_line_ptr; /* 0x0c8 */
	__u8 _pad4[112]; /* 0x0cc */
	__u32 cc_blob_address; /* 0x13c */
	struct edid_info edid_info; /* 0x140 */
	struct efi_info efi_info; /* 0x1c0 */
	__u32 alt_mem_k; /* 0x1e0 */
	__u32 scratch; /* Scratch field! */ /* 0x1e4 */
	__u8 e820_entries; /* 0x1e8 */
	__u8 eddbuf_entries; /* 0x1e9 */
	__u8 edd_mbr_sig_buf_entries; /* 0x1ea */
	__u8 kbd_status; /* 0x1eb */
	__u8 secure_boot; /* 0x1ec */
	__u8 _pad5[2]; /* 0x1ed */
	/*
	 * The sentinel is set to a nonzero value (0xff) in header.S.
	 *
	 * A bootloader is supposed to only take setup_header and put
	 * it into a clean boot_params buffer. If it turns out that
	 * it is clumsy or too generous with the buffer, it most
	 * probably will pick up the sentinel variable too. The fact
	 * that this variable then is still 0xff will let kernel
	 * know that some variables in boot_params are invalid and
	 * kernel should zero out certain portions of boot_params.
	 */
	__u8 sentinel; /* 0x1ef */
	__u8 _pad6[1]; /* 0x1f0 */
	struct setup_header hdr; /* setup header */ /* 0x1f1 */
	__u8 _pad7[0x290 - 0x1f1 - sizeof(struct setup_header)];
	__u32 edd_mbr_sig_buffer[EDD_MBR_SIG_MAX]; /* 0x290 */
	struct boot_e820_entry e820_table[E820_MAX_ENTRIES_ZEROPAGE]; /* 0x2d0 */
	__u8 _pad8[48]; /* 0xcd0 */
	struct edd_info eddbuf[EDDMAXNR]; /* 0xd00 */
	__u8 _pad9[276]; /* 0xeec */
} __attribute__((packed));

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_BOOTPARAM_H */
