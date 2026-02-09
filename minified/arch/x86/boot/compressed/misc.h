 
#ifndef BOOT_COMPRESSED_MISC_H
#define BOOT_COMPRESSED_MISC_H

 
#undef CONFIG_PARAVIRT
#undef CONFIG_PARAVIRT_XXL
#undef CONFIG_PARAVIRT_SPINLOCKS
#undef CONFIG_KASAN
#undef CONFIG_KASAN_GENERIC

#define __NO_FORTIFY

 
#define USE_EARLY_PGTABLE_L5

#include <linux/linkage.h>
#include <linux/screen_info.h>
#include <linux/elf.h>
#include <asm/page.h>
#include <asm/boot.h>
#include <asm/bootparam.h>
#include <asm/desc_defs.h>

/* early_tdx_detect removed - TDX not needed */

#define BOOT_CTYPE_H
#include <linux/acpi.h>

#define BOOT_BOOT_H
#include "../ctype.h"
#include "../io.h"

/* --- Inlined efi.h --- */

#if defined(_LINUX_EFI_H) || defined(_ASM_X86_EFI_H)
#error Please do not include kernel proper namespace headers
#endif

/* Local guid_t definition - can't use uuid.h in boot/compressed */
typedef struct {
	__u8 b[16];
} guid_t;

typedef guid_t efi_guid_t __aligned(__alignof__(u32));

#define EFI_GUID(a, b, c, d...) (efi_guid_t){ {					\
	(a) & 0xff, ((a) >> 8) & 0xff, ((a) >> 16) & 0xff, ((a) >> 24) & 0xff,	\
	(b) & 0xff, ((b) >> 8) & 0xff,						\
	(c) & 0xff, ((c) >> 8) & 0xff, d } }

typedef	struct {
	u64 signature;
	u32 revision;
	u32 headersize;
	u32 crc32;
	u32 reserved;
} efi_table_hdr_t;

/* EFI_CONVENTIONAL_MEMORY removed - never used */

typedef struct {
	u32 type;
	u32 pad;
	u64 phys_addr;
	u64 virt_addr;
	u64 num_pages;
	u64 attribute;
} efi_memory_desc_t;

#define efi_early_memdesc_ptr(map, desc_size, n)			\
	(efi_memory_desc_t *)((void *)(map) + ((n) * (desc_size)))

typedef struct {
	efi_guid_t guid;
	u64 table;
} efi_config_table_64_t;

typedef struct {
	efi_guid_t guid;
	u32 table;
} efi_config_table_32_t;

typedef struct {
	efi_table_hdr_t hdr;
	u64 fw_vendor;	 
	u32 fw_revision;
	u32 __pad1;
	u64 con_in_handle;
	u64 con_in;
	u64 con_out_handle;
	u64 con_out;
	u64 stderr_handle;
	u64 stderr;
	u64 runtime;
	u64 boottime;
	u32 nr_tables;
	u32 __pad2;
	u64 tables;
} efi_system_table_64_t;

typedef struct {
	efi_table_hdr_t hdr;
	u32 fw_vendor;	 
	u32 fw_revision;
	u32 con_in_handle;
	u32 con_in;
	u32 con_out_handle;
	u32 con_out;
	u32 stderr_handle;
	u32 stderr;
	u32 runtime;
	u32 boottime;
	u32 nr_tables;
	u32 tables;
} efi_system_table_32_t;

/* struct efi_setup_data removed - never instantiated */

#define memptr unsigned

 
extern char _head[], _end[];

 
extern memptr free_mem_ptr;
extern memptr free_mem_end_ptr;
void *malloc(int size);
void free(void *where);
extern struct boot_params *boot_params;
void __putstr(const char *s);
#define error_putstr(__x)  __putstr(__x)
/* error_puthex removed - never used */


static inline void debug_putstr(const char *s)
{ while (*s) asm volatile("outb %0, $0xe9" : : "a"(*s++)); }
#define debug_putaddr(x)  


 
int cmdline_find_option(const char *option, char *buffer, int bufsize);
int cmdline_find_option_bool(const char *option);

/* struct mem_vector removed - never instantiated */
/* choose_random_location removed - call deleted (no KASLR) */

static const int early_serial_base;
/* console_init removed - was empty stub */

/* get_rsdp_addr removed - replaced with inline 0 */


/* kernel_add_identity_map removed - never defined */
extern pteval_t __default_kernel_pte_mask;

#endif  
