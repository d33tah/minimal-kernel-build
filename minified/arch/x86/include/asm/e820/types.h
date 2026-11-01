#ifndef _ASM_E820_TYPES_H
#define _ASM_E820_TYPES_H

#include <uapi/asm/bootparam.h>

/*
 * These are the E820 types known to the kernel:
 */
enum e820_type {
	E820_TYPE_RAM = 1,
};

/*
 * A single E820 map entry, describing a memory range of [addr...addr+size-1],
 * of 'type' memory type:
 *
 * (We pack it because there can be thousands of them on large systems.)
 */
struct e820_entry {
	u64 addr;
	u64 size;
	enum e820_type type;
} __attribute__((packed));

/*
 * The legacy E820 BIOS limits us to 128 (E820_MAX_ENTRIES_ZEROPAGE) nodes
 * due to the constrained space in the zeropage.
 *
 * Size our internal memory map tables to have room for these additional
 * entries, based on a heuristic calculation: up to three entries per
 * NUMA node, plus E820_MAX_ENTRIES_ZEROPAGE for some extra space.
 */

#include <linux/numa.h>

/*
 * MKB_FOOTPRINT: 32 entries (upstream: E820_MAX_ENTRIES_ZEROPAGE + 3 *
 * MAX_NUMNODES = 131); qemu passes 6, __e820__range_add ignores the rest.
 */
#define E820_MAX_ENTRIES 32

/*
 * The whole array of E820 entries:
 */
struct e820_table {
	__u32 nr_entries;
	struct e820_entry entries[E820_MAX_ENTRIES];
};

/*
 * Various well-known legacy memory ranges in physical memory:
 */
#define ISA_END_ADDRESS 0x00100000

#endif /* _ASM_E820_TYPES_H */
