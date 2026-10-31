#include <linux/memblock.h>

#include <asm/e820/api.h>
#include <asm/setup.h>

/*
 * The BIOS E820 table the real-mode setup code stored in the zero page is
 * copied into e820_table and used from there: RAM entries go to memblock,
 * the highest RAM end below 4 GiB is max_pfn. There is no sanitising
 * (e820__update_table() is not here: overlapping or unsorted BIOS entries
 * are used as given), no E801/88 fallback, no SETUP_E820_EXT extension and
 * no kexec/firmware copies of the table.
 */
static struct e820_table e820_table_init __initdata;

struct e820_table *e820_table __refdata = &e820_table_init;

/*
 * Add a memory region to the kernel E820 map.
 */
static void __init __e820__range_add(struct e820_table *table, u64 start,
				     u64 size, enum e820_type type)
{
	int x = table->nr_entries;

	if (x >= ARRAY_SIZE(table->entries))
		return;

	table->entries[x].addr = start;
	table->entries[x].size = size;
	table->entries[x].type = type;
	table->nr_entries++;
}

void __init e820__range_add(u64 start, u64 size, enum e820_type type)
{
	__e820__range_add(e820_table, start, size, type);
}

static int __init __append_e820_table(struct boot_e820_entry *entries,
				      u32 nr_entries)
{
	struct boot_e820_entry *entry = entries;

	while (nr_entries) {
		u64 start = entry->addr;
		u64 size = entry->size;
		u64 end = start + size - 1;
		u32 type = entry->type;

		/* Ignore the entry on 64-bit overflow: */
		if (start > end && likely(size))
			return -1;

		e820__range_add(start, size, type);

		entry++;
		nr_entries--;
	}
	return 0;
}

/*
 * Copy the BIOS E820 map into a safe place.
 *
 * Sanity-check it while we're at it..
 */
static int __init append_e820_table(struct boot_e820_entry *entries,
				    u32 nr_entries)
{
	/* Only one memory region (or negative)? Ignore it */
	if (nr_entries < 2)
		return -1;

	return __append_e820_table(entries, nr_entries);
}

#define MAX_ARCH_PFN (1ULL << (32 - PAGE_SHIFT))

/*
 * Find the highest page frame number we have available
 */
static unsigned long __init e820_end_pfn(unsigned long limit_pfn,
					 enum e820_type type)
{
	int i;
	unsigned long last_pfn = 0;
	unsigned long max_arch_pfn = MAX_ARCH_PFN;

	for (i = 0; i < e820_table->nr_entries; i++) {
		struct e820_entry *entry = &e820_table->entries[i];
		unsigned long start_pfn;
		unsigned long end_pfn;

		if (entry->type != type)
			continue;

		start_pfn = entry->addr >> PAGE_SHIFT;
		end_pfn = (entry->addr + entry->size) >> PAGE_SHIFT;

		if (start_pfn >= limit_pfn)
			continue;
		if (end_pfn > limit_pfn) {
			last_pfn = limit_pfn;
			break;
		}
		if (end_pfn > last_pfn)
			last_pfn = end_pfn;
	}

	if (last_pfn > max_arch_pfn)
		last_pfn = max_arch_pfn;

	return last_pfn;
}

unsigned long __init e820__end_of_ram_pfn(void)
{
	return e820_end_pfn(MAX_ARCH_PFN, E820_TYPE_RAM);
}

/*
 * Pass the firmware (bootloader) E820 map to the kernel and process it:
 */
void __init e820__memory_setup_default(void)
{
	/*
	 * Try to copy the BIOS-supplied E820-map. There is no E801/88
	 * fallback to fake one from.
	 */
	if (append_e820_table(boot_params.e820_table,
			      boot_params.e820_entries) < 0)
		panic("no BIOS E820 memory map");
}

void __init e820__memory_setup(void)
{
	/* This is a firmware interface ABI - make sure we don't break it: */
	BUILD_BUG_ON(sizeof(struct boot_e820_entry) != 20);

	e820__memory_setup_default();
}

void __init e820__memblock_setup(void)
{
	int i;
	u64 end;

	for (i = 0; i < e820_table->nr_entries; i++) {
		struct e820_entry *entry = &e820_table->entries[i];

		end = entry->addr + entry->size;
		if (end != (resource_size_t)end)
			continue;

		if (entry->type != E820_TYPE_RAM)
			continue;

		memblock_add(entry->addr, entry->size);
	}

	/* Throw away partial pages: */
	memblock_trim_memory(PAGE_SIZE);
}
