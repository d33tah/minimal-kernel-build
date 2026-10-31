
#ifndef BOOT_COMPRESSED_MISC_H
#define BOOT_COMPRESSED_MISC_H

#undef CONFIG_PARAVIRT
#undef CONFIG_PARAVIRT_XXL
#undef CONFIG_PARAVIRT_SPINLOCKS
#undef CONFIG_KASAN
#undef CONFIG_KASAN_GENERIC

#define __NO_FORTIFY

#include <asm/page.h>
#include <asm/boot.h>

#define BOOT_BOOT_H
#include "../io.h"

extern char _head[], _end[];

extern struct boot_params *boot_params;
void __putstr(const char *s);

#endif
