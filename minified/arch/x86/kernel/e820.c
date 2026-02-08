#include <asm/early_ioremap.h>
#include <linux/memblock.h>
/* linux/swap.h removed - no swap functions used */
#include <linux/init.h>
/* linux/pm.h removed - no pm functions used */
#include <linux/mm.h>
#include <asm/errno.h>
#include <linux/acpi.h>
#include <linux/sort.h>
/* linux/memory_hotplug.h removed - not used */
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

/* e820_print_type, e820__print_table stubs removed - never called */

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

static u64 __init __e820__range_update(struct e820_table *table, u64 start,
				       u64 size, enum e820_type old_type,
				       enum e820_type new_type)
{
	u64 end;
	unsigned int i;
	u64 real_updated_size = 0;

	BUG_ON(old_type == new_type);

	if (size > (ULLONG_MAX - start))
		size = ULLONG_MAX - start;

	end = start + size;
	/* Debug printk and e820_print_type calls removed - not needed */

	for (i = 0; i < table->nr_entries; i++) {
		struct e820_entry *entry = &table->entries[i];
		u64 final_start, final_end;
		u64 entry_end;

		if (entry->type != old_type)
			continue;

		entry_end = entry->addr + entry->size;

		if (entry->addr >= start && entry_end <= end) {
			entry->type = new_type;
			real_updated_size += entry->size;
			continue;
		}

		if (entry->addr < start && entry_end > end) {
			__e820__range_add(table, start, size, new_type);
			__e820__range_add(table, end, entry_end - end,
					  entry->type);
			entry->size = start - entry->addr;
			real_updated_size += size;
			continue;
		}

		final_start = max(start, entry->addr);
		final_end = min(end, entry_end);
		if (final_start >= final_end)
			continue;

		__e820__range_add(table, final_start, final_end - final_start,
				  new_type);

		real_updated_size += final_end - final_start;

		entry->size -= final_end - final_start;
		if (entry->addr < final_start)
			continue;

		entry->addr = final_end;
	}
	return real_updated_size;
}

u64 __init e820__range_update(u64 start, u64 size, enum e820_type old_type,
			      enum e820_type new_type)
{
	return __e820__range_update(e820_table, start, size, old_type,
				    new_type);
}

u64 __init e820__range_remove(u64 start, u64 size, enum e820_type old_type,
			      bool check_type)
{
	int i;
	u64 end;
	u64 real_removed_size = 0;

	if (size > (ULLONG_MAX - start))
		size = ULLONG_MAX - start;

	end = start + size;
	/* Debug printk and e820_print_type calls removed - not needed */

	for (i = 0; i < e820_table->nr_entries; i++) {
		struct e820_entry *entry = &e820_table->entries[i];
		u64 final_start, final_end;
		u64 entry_end;

		if (check_type && entry->type != old_type)
			continue;

		entry_end = entry->addr + entry->size;

		if (entry->addr >= start && entry_end <= end) {
			real_removed_size += entry->size;
			memset(entry, 0, sizeof(*entry));
			continue;
		}

		if (entry->addr < start && entry_end > end) {
			e820__range_add(end, entry_end - end, entry->type);
			entry->size = start - entry->addr;
			real_removed_size += size;
			continue;
		}

		final_start = max(start, entry->addr);
		final_end = min(end, entry_end);
		if (final_start >= final_end)
			continue;

		real_removed_size += final_end - final_start;

		entry->size -= final_end - final_start;
		if (entry->addr < final_start)
			continue;

		entry->addr = final_end;
	}
	return real_removed_size;
}

/* e820__update_table_print removed - never called */

#define MAX_GAP_END 0x100000000ull

__init void e820__setup_pci_gap(void)
{
	unsigned long gapstart = 0x10000000;
	unsigned long gapsize = 0x400000;
	unsigned long long last = MAX_GAP_END;
	int i = e820_table->nr_entries;

	/* Inlined e820_search_gap */
	while (--i >= 0) {
		unsigned long long start = e820_table->entries[i].addr;
		unsigned long long end = start + e820_table->entries[i].size;

		if (last > end) {
			unsigned long gap = last - end;
			if (gap >= gapsize) {
				gapsize = gap;
				gapstart = end;
			}
		}
		if (start < last)
			last = start;
	}

	/* pci_mem_start removed - never read */

	pr_info("[mem %#010lx-%#010lx] available for PCI devices\n", gapstart,
		gapstart + gapsize - 1);
}

/* e820__reallocate_tables removed - never called */

void __init e820__memory_setup_extended(u64 phys_addr, u32 data_len)
{
	int entries;
	struct boot_e820_entry *extmap;
	struct setup_data *sdata;

	sdata = early_memremap(phys_addr, data_len);
	entries = sdata->len / sizeof(*extmap);
	extmap = (struct boot_e820_entry *)(sdata->data);

	__append_e820_table(extmap, entries);
	e820__update_table(e820_table);
	/* kexec/firmware table copy removed - unused in minimal kernel */

	early_memunmap(sdata, data_len);
}

/* e820__register_nosave_regions removed - call site eliminated */

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

/* e820__end_of_low_ram_pfn removed - never called */

/* userdef removed - never written, always 0 */

void __init e820__reserve_setup_data(void)
{
	struct setup_indirect *indirect;
	struct setup_data *data;
	u64 pa_data, pa_next;
	u32 len;

	pa_data = boot_params.hdr.setup_data;
	if (!pa_data)
		return;

	while (pa_data) {
		data = early_memremap(pa_data, sizeof(*data));
		if (!data) {
			pr_warn("e820: failed to memremap setup_data entry\n");
			return;
		}

		len = sizeof(*data);
		pa_next = data->next;

		e820__range_update(pa_data, sizeof(*data) + data->len,
				   E820_TYPE_RAM, E820_TYPE_RESERVED_KERN);
		/* kexec table update removed - unused in minimal kernel */

		if (data->type == SETUP_INDIRECT) {
			len += data->len;
			early_memunmap(data, sizeof(*data));
			data = early_memremap(pa_data, len);
			if (!data) {
				pr_warn("e820: failed to memremap indirect setup_data\n");
				return;
			}

			indirect = (struct setup_indirect *)data->data;

			if (indirect->type != SETUP_INDIRECT) {
				e820__range_update(indirect->addr,
						   indirect->len, E820_TYPE_RAM,
						   E820_TYPE_RESERVED_KERN);
				/* kexec table update removed */
			}
		}

		pa_data = pa_next;
		early_memunmap(data, len);
	}

	e820__update_table(e820_table);
	/* kexec table update removed - unused in minimal kernel */
}

/* e820__finish_early_params removed - body was empty (~3 LOC) */
/* e820_type_to_string, e820_type_to_iomem_type, e820_type_to_iores_desc, do_mark_busy
   inlined into e820__reserve_resources */

/* e820_res removed - only written, never read */

void __init e820__reserve_resources(void)
{
	int i;
	struct resource *res;
	u64 end;

	res = memblock_alloc(sizeof(*res) * e820_table->nr_entries,
			     SMP_CACHE_BYTES);
	if (!res)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      sizeof(*res) * e820_table->nr_entries);

	for (i = 0; i < e820_table->nr_entries; i++) {
		struct e820_entry *entry = e820_table->entries + i;
		bool is_ram = entry->type == E820_TYPE_RESERVED_KERN ||
			      entry->type == E820_TYPE_RAM;

		end = entry->addr + entry->size - 1;
		if (end != (resource_size_t)end) {
			res++;
			continue;
		}
		res->start = entry->addr;
		res->end = end;
		res->name = is_ram ? "System RAM" : "Reserved";
		res->flags = is_ram ? IORESOURCE_SYSTEM_RAM : IORESOURCE_MEM;
		res->desc = IORES_DESC_NONE;
		res->flags |= IORESOURCE_BUSY;
		insert_resource(&iomem_resource, res);
		res++;
	}
}

char *__init e820__memory_setup_default(void)
{
	char *who = "BIOS-e820";

	/* Inlined append_e820_table check */
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
	/* kexec/firmware table copies removed - unused in minimal kernel */
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
	/* memblock_dump_all removed - was empty stub */
}
