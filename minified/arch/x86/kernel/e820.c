#include <linux/memblock.h>

#include <asm/e820/api.h>
#include <asm/setup.h>

/*
 * The memory map is the BIOS E820 table the real-mode setup code stored in
 * the zero page, used as is: RAM entries go to memblock, the highest RAM
 * end below 4 GiB is max_pfn. There is no sanitising/merging of overlapping
 * entries, no E801/88 fallback and no SETUP_E820_EXT extension.
 */

#define MAX_ARCH_PFN (1ULL << (32 - PAGE_SHIFT))

void __init e820__memory_setup(void)
{
	BUILD_BUG_ON(sizeof(struct boot_e820_entry) != 20);

	if (!boot_params.e820_entries)
		panic("no BIOS E820 memory map");
}

unsigned long __init e820__end_of_ram_pfn(void)
{
	unsigned long last_pfn = 0;
	int i;

	for (i = 0; i < boot_params.e820_entries; i++) {
		struct boot_e820_entry *entry = &boot_params.e820_table[i];
		u64 end_pfn = (entry->addr + entry->size) >> PAGE_SHIFT;

		if (entry->type != E820_TYPE_RAM ||
		    entry->addr >> PAGE_SHIFT >= MAX_ARCH_PFN)
			continue;
		if (end_pfn > MAX_ARCH_PFN)
			end_pfn = MAX_ARCH_PFN;
		if (end_pfn > last_pfn)
			last_pfn = end_pfn;
	}

	return last_pfn;
}

void __init e820__memblock_setup(void)
{
	int i;

	memblock_allow_resize();

	for (i = 0; i < boot_params.e820_entries; i++) {
		struct boot_e820_entry *entry = &boot_params.e820_table[i];
		u64 end = entry->addr + entry->size;

		if (entry->type != E820_TYPE_RAM || end != (resource_size_t)end)
			continue;

		memblock_add(entry->addr, entry->size);
	}

	memblock_trim_memory(PAGE_SIZE);
}
