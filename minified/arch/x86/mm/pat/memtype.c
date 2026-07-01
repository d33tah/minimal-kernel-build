// Stubbed version of PAT (Page Attribute Table) memory type management
// Original: 591 LOC

#include <asm/memtype.h>


#undef pr_fmt
#define pr_fmt(fmt) "" fmt

void pat_disable(const char *msg_reason) { }

void init_cache_modes(void) { }
