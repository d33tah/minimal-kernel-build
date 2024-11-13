// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains work-arounds for x86 and x86_64 platform bugs.
 */
#include <linux/dmi.h>
#include <linux/pci.h>
#include <linux/irq.h>

#include <asm/hpet.h>
#include <asm/setup.h>
#include <asm/mce.h>



#if defined(CONFIG_PCI) && defined(CONFIG_NUMA)
/* Set correct numa_node information for AMD NB functions */
static void quirk_amd_nb_node(struct pci_dev *dev)
{
	struct pci_dev *nb_ht;
	unsigned int devfn;
	u32 node;
	u32 val;

	devfn = PCI_DEVFN(PCI_SLOT(dev->devfn), 0);
	nb_ht = pci_get_slot(dev->bus, devfn);
	if (!nb_ht)
		return;

	pci_read_config_dword(nb_ht, 0x60, &val);
	node = pcibus_to_node(dev->bus) | (val & 7);
	/*
	 * Some hardware may return an invalid node ID,
	 * so check it first:
	 */
	if (node_online(node))
		set_dev_node(&dev->dev, node);
	pci_dev_put(nb_ht);
}

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB_ADDRMAP,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB_MEMCTL,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB_MISC,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_10H_NB_HT,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_10H_NB_MAP,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_10H_NB_DRAM,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_10H_NB_MISC,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_10H_NB_LINK,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_15H_NB_F0,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_15H_NB_F1,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_15H_NB_F2,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_15H_NB_F3,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_15H_NB_F4,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_15H_NB_F5,
			quirk_amd_nb_node);

#endif

#ifdef CONFIG_PCI
/*
 * Processor does not ensure DRAM scrub read/write sequence
 * is atomic wrt accesses to CC6 save state area. Therefore
 * if a concurrent scrub read/write access is to same address
 * the entry may appear as if it is not written. This quirk
 * applies to Fam16h models 00h-0Fh
 *
 * See "Revision Guide" for AMD F16h models 00h-0fh,
 * document 51810 rev. 3.04, Nov 2013
 */
static void amd_disable_seq_and_redirect_scrub(struct pci_dev *dev)
{
	u32 val;

	/*
	 * Suggested workaround:
	 * set D18F3x58[4:0] = 00h and set D18F3x5C[0] = 0b
	 */
	pci_read_config_dword(dev, 0x58, &val);
	if (val & 0x1F) {
		val &= ~(0x1F);
		pci_write_config_dword(dev, 0x58, val);
	}

	pci_read_config_dword(dev, 0x5C, &val);
	if (val & BIT(0)) {
		val &= ~BIT(0);
		pci_write_config_dword(dev, 0x5c, val);
	}
}

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_16H_NB_F3,
			amd_disable_seq_and_redirect_scrub);

/* Ivy Bridge, Haswell, Broadwell */
static void quirk_intel_brickland_xeon_ras_cap(struct pci_dev *pdev)
{
	u32 capid0;

	pci_read_config_dword(pdev, 0x84, &capid0);

	if (capid0 & 0x10)
		enable_copy_mc_fragile();
}

/* Skylake */
static void quirk_intel_purley_xeon_ras_cap(struct pci_dev *pdev)
{
	u32 capid0, capid5;

	pci_read_config_dword(pdev, 0x84, &capid0);
	pci_read_config_dword(pdev, 0x98, &capid5);

	/*
	 * CAPID0{7:6} indicate whether this is an advanced RAS SKU
	 * CAPID5{8:5} indicate that various NVDIMM usage modes are
	 * enabled, so memory machine check recovery is also enabled.
	 */
	if ((capid0 & 0xc0) == 0xc0 || (capid5 & 0x1e0))
		enable_copy_mc_fragile();

}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x0ec3, quirk_intel_brickland_xeon_ras_cap);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x2fc0, quirk_intel_brickland_xeon_ras_cap);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x6fc0, quirk_intel_brickland_xeon_ras_cap);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x2083, quirk_intel_purley_xeon_ras_cap);
#endif

bool x86_apple_machine;
EXPORT_SYMBOL(x86_apple_machine);

void __init early_platform_quirks(void)
{
	x86_apple_machine = dmi_match(DMI_SYS_VENDOR, "Apple Inc.") ||
			    dmi_match(DMI_SYS_VENDOR, "Apple Computer, Inc.");
}
