
#ifndef _ASM_E820_API_H
#define _ASM_E820_API_H

/* --- 2025-12-07 20:10 --- Inlined e820/types.h */
#include <uapi/asm/bootparam.h>

enum e820_type {
	E820_TYPE_RAM		= 1,
	E820_TYPE_RESERVED	= 2,
	E820_TYPE_ACPI		= 3,
	E820_TYPE_NVS		= 4,
	E820_TYPE_UNUSABLE	= 5,
	E820_TYPE_PMEM		= 7,
	E820_TYPE_PRAM		= 12,
	E820_TYPE_SOFT_RESERVED	= 0xefffffff,
	E820_TYPE_RESERVED_KERN	= 128,
};

struct e820_entry {
	u64			addr;
	u64			size;
	enum e820_type		type;
} __attribute__((packed));

#include <linux/numa.h>

#define E820_MAX_ENTRIES	(E820_MAX_ENTRIES_ZEROPAGE + 3*MAX_NUMNODES)

struct e820_table {
	__u32 nr_entries;
	struct e820_entry entries[E820_MAX_ENTRIES];
};

#define ISA_START_ADDRESS	0x000a0000
#define ISA_END_ADDRESS		0x00100000
#define BIOS_BEGIN		0x000a0000
#define BIOS_END		0x00100000
#define HIGH_MEMORY		0x00100000
#define BIOS_ROM_BASE		0xffe00000
#define BIOS_ROM_END		0xffffffff
/* --- end inlined types.h --- */

extern struct e820_table *e820_table;
/* e820_table_kexec/firmware removed - unused in minimal kernel */

extern unsigned long pci_mem_start;

extern bool e820__mapped_any(u64 start, u64 end, enum e820_type type);
extern bool e820__mapped_all(u64 start, u64 end, enum e820_type type);

extern void e820__range_add   (u64 start, u64 size, enum e820_type type);
extern u64  e820__range_update(u64 start, u64 size, enum e820_type old_type, enum e820_type new_type);
extern u64  e820__range_remove(u64 start, u64 size, enum e820_type old_type, bool check_type);

extern void e820__print_table(char *who);
extern int  e820__update_table(struct e820_table *table);
extern void e820__update_table_print(void);

extern unsigned long e820__end_of_ram_pfn(void);
extern unsigned long e820__end_of_low_ram_pfn(void);

extern void e820__memblock_setup(void);

extern void e820__reserve_setup_data(void);
extern void e820__finish_early_params(void);
extern void e820__reserve_resources(void);

extern void e820__memory_setup(void);
extern void e820__memory_setup_extended(u64 phys_addr, u32 data_len);
extern char *e820__memory_setup_default(void);
extern void e820__setup_pci_gap(void);

extern void e820__reallocate_tables(void);
extern void e820__register_nosave_regions(unsigned long limit_pfn);

static inline bool is_ISA_range(u64 start, u64 end)
{
	return start >= ISA_START_ADDRESS && end <= ISA_END_ADDRESS;
}

#endif  
