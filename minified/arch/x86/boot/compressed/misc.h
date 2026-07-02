 
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

#include "tdx.h"

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


static inline void debug_putstr(const char *s)
{ }
static inline void debug_puthex(unsigned long value)
{ }
#define debug_putaddr(x)  


 

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

typedef u64 acpi_physical_address;
static inline acpi_physical_address get_rsdp_addr(void) { return 0; }


extern pteval_t __default_kernel_pte_mask;

static inline void cleanup_exception_handling(void) { }

#endif
