 
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

#define BOOT_CTYPE_H
#define BOOT_BOOT_H
#include "../io.h"

#if defined(_LINUX_EFI_H) || defined(_ASM_X86_EFI_H)
#error Please do not include kernel proper namespace headers
#endif

#define memptr unsigned

extern char _head[], _end[];

extern memptr free_mem_ptr;
extern memptr free_mem_end_ptr;
void *malloc(int size);
void free(void *where);
extern struct boot_params *boot_params;
void __putstr(const char *s);
#define error_putstr(__x)  __putstr(__x)

static inline void debug_putstr(const char *s)
{ while (*s) asm volatile("outb %0, $0xe9" : : "a"(*s++)); }
#define debug_putaddr(x)  


static const int early_serial_base;

extern pteval_t __default_kernel_pte_mask;

#endif  
