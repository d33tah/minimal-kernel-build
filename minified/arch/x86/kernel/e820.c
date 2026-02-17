#include <linux/memblock.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <asm/errno.h>
void sort(void *base, size_t num, size_t size, cmp_func_t cmp_func,
	  swap_func_t swap_func);
#include <asm/e820/api.h>
#include <asm/setup.h>

static struct e820_table e820_table_init __initdata;

struct e820_table *e820_table __refdata = &e820_table_init;

/* __e820__mapped_all merged into e820__mapped_all */
bool __init e820__mapped_all(u64 start, u64 end, enum e820_type type)
{
	int i;
	for (i = 0; i < e820_table->nr_entries; i++) {
		struct e820_entry *entry = &e820_table->entries[i];
		if (type && entry->type != type)
			continue;
		if (entry->addr >= end || entry->addr + entry->size <= start)
			continue;
		if (entry->addr <= start)
			start = entry->addr + entry->size;
		if (start >= end)
			return true;
	}
	return false;
}

static void __init __e820__range_add(struct e820_table *table, u64 start,
				     u64 size, enum e820_type type)
{
	int x = table->nr_entries;

	if (x >= ARRAY_SIZE(table->entries)) {
		pr_err("too many entries; ignoring [mem %#010llx-%#010llx]\n",
		       start, start + size - 1);
		return;
	}

	table->entries[x].addr = start;
	table->entries[x].size = size;
	table->entries[x].type = type;
	table->nr_entries++;
}

void __init e820__range_add(u64 start, u64 size, enum e820_type type)
{
	__e820__range_add(e820_table, start, size, type);
}

/* change_member, change_point_list, overlap_list, new_entries, cpcompare
 * removed - e820__update_table stubbed for QEMU */

/* e820 table merging stubbed - QEMU provides clean non-overlapping map */
int __init e820__update_table(struct e820_table *table)
{
	return 0;
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

		if (start > end && likely(size))
			return -1;

		e820__range_add(start, size, type);

		entry++;
		nr_entries--;
	}
	return 0;
}

/* append_e820_table merged into e820__memory_setup_default */

/* Simplified: just change matching entry types in-place */
u64 __init e820__range_update(u64 start, u64 size, enum e820_type old_type,
			      enum e820_type new_type)
{
	unsigned int i;
	u64 real_updated_size = 0;
	u64 end = start + size;

	for (i = 0; i < e820_table->nr_entries; i++) {
		struct e820_entry *entry = &e820_table->entries[i];
		if (entry->type != old_type)
			continue;
		if (entry->addr >= start && entry->addr + entry->size <= end) {
			entry->type = new_type;
			real_updated_size += entry->size;
		}
	}
	return real_updated_size;
}

/* Simplified: just zero matching entries */
u64 __init e820__range_remove(u64 start, u64 size, enum e820_type old_type,
			      bool check_type)
{
	unsigned int i;
	u64 real_removed_size = 0;
	u64 end = start + size;

	for (i = 0; i < e820_table->nr_entries; i++) {
		struct e820_entry *entry = &e820_table->entries[i];
		if (check_type && entry->type != old_type)
			continue;
		if (entry->addr >= start && entry->addr + entry->size <= end) {
			real_removed_size += entry->size;
			memset(entry, 0, sizeof(*entry));
		}
	}
	return real_removed_size;
}

__init void e820__setup_pci_gap(void)
{
}

#define MAX_ARCH_PFN (1ULL << (32 - PAGE_SHIFT))

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

	pr_info("last_pfn = %#lx max_arch_pfn = %#lx\n", last_pfn,
		max_arch_pfn);
	return last_pfn;
}

unsigned long __init e820__end_of_ram_pfn(void)
{
	return e820_end_pfn(MAX_ARCH_PFN, E820_TYPE_RAM);
}

/* Simplified: QEMU has no setup_data */
void __init e820__reserve_setup_data(void)
{
}

/* e820_type_to_string, e820_type_to_iomem_type, e820_type_to_iores_desc, do_mark_busy
   inlined into e820__reserve_resources */

void __init e820__reserve_resources(void)
{
}

char *__init e820__memory_setup_default(void)
{
	char *who = "BIOS-e820";

	if (boot_params.e820_entries < 2 ||
	    __append_e820_table(boot_params.e820_table,
				boot_params.e820_entries) < 0) {
		u64 mem_size;

		if (boot_params.alt_mem_k < boot_params.screen_info.ext_mem_k) {
			mem_size = boot_params.screen_info.ext_mem_k;
			who = "BIOS-88";
		} else {
			mem_size = boot_params.alt_mem_k;
			who = "BIOS-e801";
		}

		e820_table->nr_entries = 0;
		e820__range_add(0, LOWMEMSIZE(), E820_TYPE_RAM);
		e820__range_add(HIGH_MEMORY, mem_size << 10, E820_TYPE_RAM);
	}

	e820__update_table(e820_table);

	return who;
}

void __init e820__memory_setup(void)
{
	BUILD_BUG_ON(sizeof(struct boot_e820_entry) != 20);

	x86_init.resources.memory_setup();
}

void __init e820__memblock_setup(void)
{
	int i;
	u64 end;

	memblock_allow_resize();

	for (i = 0; i < e820_table->nr_entries; i++) {
		struct e820_entry *entry = &e820_table->entries[i];

		end = entry->addr + entry->size;
		if (end != (resource_size_t)end)
			continue;

		if (entry->type == E820_TYPE_SOFT_RESERVED)
			memblock_reserve(entry->addr, entry->size);

		if (entry->type != E820_TYPE_RAM &&
		    entry->type != E820_TYPE_RESERVED_KERN)
			continue;

		memblock_add(entry->addr, entry->size);
	}

	memblock_trim_memory(PAGE_SIZE);
}
