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
#include <asm/sev.h>

struct real_mode_header *real_mode_header;
u32 *trampoline_cr4_features;

pgd_t trampoline_pgd_entry;

void load_trampoline_pgtable(void)
{
	load_cr3(initial_page_table);

	__flush_tlb_all();
}

void __init reserve_real_mode(void)
{
	/* Stubbed - not needed for Hello World (SMP trampoline) */
	memblock_reserve(0, SZ_1M);
}

static void __init sme_sev_setup_real_mode(struct trampoline_header *th)
{
}

static void __init setup_real_mode(void)
{
	u16 real_mode_seg;
	const u32 *rel;
	u32 count;
	unsigned char *base;
	unsigned long phys_base;
	struct trampoline_header *trampoline_header;
	size_t size = PAGE_ALIGN(real_mode_blob_end - real_mode_blob);

	base = (unsigned char *)real_mode_header;

	if (cc_platform_has(CC_ATTR_HOST_MEM_ENCRYPT))
		set_memory_decrypted((unsigned long)base, size >> PAGE_SHIFT);

	memcpy(base, real_mode_blob, size);

	phys_base = __pa(base);
	real_mode_seg = phys_base >> 4;

	rel = (u32 *)real_mode_relocs;

	count = *rel++;
	while (count--) {
		u16 *seg = (u16 *)(base + *rel++);
		*seg = real_mode_seg;
	}

	count = *rel++;
	while (count--) {
		u32 *ptr = (u32 *)(base + *rel++);
		*ptr += phys_base;
	}

	trampoline_header = (struct trampoline_header *)__va(
		real_mode_header->trampoline_header);

	trampoline_header->start = __pa_symbol(startup_32_smp);
	trampoline_header->gdt_limit = __BOOT_DS + 7;
	trampoline_header->gdt_base = __pa_symbol(boot_gdt);

	sme_sev_setup_real_mode(trampoline_header);
}

static void __init set_real_mode_permissions(void)
{
	unsigned char *base = (unsigned char *)real_mode_header;
	size_t size = PAGE_ALIGN(real_mode_blob_end - real_mode_blob);

	size_t ro_size = PAGE_ALIGN(real_mode_header->ro_end) - __pa(base);

	size_t text_size = PAGE_ALIGN(real_mode_header->ro_end) -
			   real_mode_header->text_start;

	unsigned long text_start =
		(unsigned long)__va(real_mode_header->text_start);

	set_memory_nx((unsigned long)base, size >> PAGE_SHIFT);
	set_memory_ro((unsigned long)base, ro_size >> PAGE_SHIFT);
	set_memory_x((unsigned long)text_start, text_size >> PAGE_SHIFT);
}

static int __init init_real_mode(void)
{
	/* Stubbed - not needed for Hello World */
	return 0;
}
early_initcall(init_real_mode);
