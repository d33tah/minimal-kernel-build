#include <linux/io.h>
#include <linux/slab.h>
#include <linux/memblock.h>
enum cc_attr {
	CC_ATTR_HOST_MEM_ENCRYPT,
	CC_ATTR_GUEST_MEM_ENCRYPT,
	CC_ATTR_GUEST_UNROLL_STRING_IO
};
static inline bool cc_platform_has(enum cc_attr attr)
{
	return false;
}
#include <linux/pgtable.h>

#include <asm/set_memory.h>
#include <asm/realmode.h>
#include <asm/tlbflush.h>
/* asm/sev.h include removed - file is stub, nothing used */

struct real_mode_header *real_mode_header;
u32 *trampoline_cr4_features;
/* trampoline_pgd_entry removed - unused */

/* load_trampoline_pgtable removed - unused */

void __init reserve_real_mode(void)
{
	/* Stubbed - not needed for Hello World (SMP trampoline) */
	memblock_reserve(0, SZ_1M);
}

/* sme_sev_setup_real_mode, setup_real_mode, set_real_mode_permissions removed
   - init_real_mode is stubbed and never calls them */

static int __init init_real_mode(void)
{
	/* Stubbed - not needed for Hello World */
	return 0;
}
early_initcall(init_real_mode);
