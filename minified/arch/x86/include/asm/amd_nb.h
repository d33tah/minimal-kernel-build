/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_AMD_NB_H
#define _ASM_X86_AMD_NB_H

#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/refcount.h>

struct amd_nb_bus_dev_range {
	u8 bus;
	u8 dev_base;
	u8 dev_limit;
};

extern const struct amd_nb_bus_dev_range amd_nb_bus_dev_ranges[];

extern bool early_is_amd_nb(u32 value);
extern struct resource *amd_get_mmconfig_range(struct resource *res);
extern void amd_flush_garts(void);
extern int amd_numa_init(void);
extern int amd_get_subcaches(int);
extern int amd_set_subcaches(int, unsigned long);

extern int amd_smn_read(u16 node, u32 address, u32 *value);
extern int amd_smn_write(u16 node, u32 address, u32 value);

struct amd_l3_cache {
	unsigned indices;
	u8	 subcaches[4];
};

struct threshold_block {
	unsigned int	 block;			/* Number within bank */
	unsigned int	 bank;			/* MCA bank the block belongs to */
	unsigned int	 cpu;			/* CPU which controls MCA bank */
	u32		 address;		/* MSR address for the block */
	u16		 interrupt_enable;	/* Enable/Disable APIC interrupt */
	bool		 interrupt_capable;	/* Bank can generate an interrupt. */

	u16		 threshold_limit;	/*
						 * Value upon which threshold
						 * interrupt is generated.
						 */

	struct kobject	 kobj;			/* sysfs object */
	struct list_head miscj;			/*
						 * List of threshold blocks
						 * within a bank.
						 */
};

struct threshold_bank {
	struct kobject		*kobj;
	struct threshold_block	*blocks;

	/* initialized to the number of CPUs on the node sharing this bank */
	refcount_t		cpus;
	unsigned int		shared;
};

struct amd_northbridge {
	struct pci_dev *root;
	struct pci_dev *misc;
	struct pci_dev *link;
	struct amd_l3_cache l3_cache;
	struct threshold_bank *bank4;
};

struct amd_northbridge_info {
	u16 num;
	u64 flags;
	struct amd_northbridge *nb;
};

#define AMD_NB_GART			BIT(0)
#define AMD_NB_L3_INDEX_DISABLE		BIT(1)
#define AMD_NB_L3_PARTITIONING		BIT(2)


#define amd_nb_num(x)		0
#define amd_nb_has_feature(x)	false
#define node_to_amd_nb(x)	NULL
#define amd_gart_present(x)	false



#endif /* _ASM_X86_AMD_NB_H */
