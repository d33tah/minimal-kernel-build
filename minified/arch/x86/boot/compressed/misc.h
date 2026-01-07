 
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

static inline void early_tdx_detect(void) { }

#define BOOT_CTYPE_H
#include <linux/acpi.h>

#define BOOT_BOOT_H
#include "../ctype.h"
#include "../io.h"

#include "efi.h"

#define memptr unsigned

 
extern char _head[], _end[];

 
extern memptr free_mem_ptr;
extern memptr free_mem_end_ptr;
void *malloc(int size);
void free(void *where);
extern struct boot_params *boot_params;
void __putstr(const char *s);
void __puthex(unsigned long value);
#define error_putstr(__x)  __putstr(__x)
#define error_puthex(__x)  __puthex(__x)


static inline void debug_putstr(const char *s)
{ while (*s) asm volatile("outb %0, $0xe9" : : "a"(*s++)); }
#define debug_putaddr(x)  


 
int cmdline_find_option(const char *option, char *buffer, int bufsize);
int cmdline_find_option_bool(const char *option);

struct mem_vector {
	u64 start;
	u64 size;
};

static inline void choose_random_location(unsigned long input,
					  unsigned long input_size,
					  unsigned long *output,
					  unsigned long output_size,
					  unsigned long *virt_addr)
{
}

static const int early_serial_base;
static inline void console_init(void)
{ }


 
/* get_rsdp_addr removed - replaced with inline 0 */


/* kernel_add_identity_map removed - never defined */
extern pteval_t __default_kernel_pte_mask;


extern gate_desc boot_idt[BOOT_IDT_ENTRIES];
/* boot_idt_desc removed - never used */

static inline void cleanup_exception_handling(void) { }

 
void boot_page_fault(void);
void boot_stage1_vc(void);
void boot_stage2_vc(void);

unsigned long sev_verify_cbit(unsigned long cr3);

enum efi_type {
	EFI_TYPE_64,
	EFI_TYPE_32,
	EFI_TYPE_NONE,
};


#endif  
