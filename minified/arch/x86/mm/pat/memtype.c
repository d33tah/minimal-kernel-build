// Stubbed version of PAT (Page Attribute Table) memory type management
// Original: 591 LOC

#include <linux/kernel.h>
#include <linux/mm.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/x86_init.h>
#include <asm/fcntl.h>
#include <asm/e820/api.h>
#include <asm/page.h>
#include <asm/msr.h>
#include <asm/memtype.h>


#undef pr_fmt
#define pr_fmt(fmt) "" fmt

void pat_disable(const char *msg_reason) { }

void init_cache_modes(void) { }
