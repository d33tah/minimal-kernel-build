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





bool x86_apple_machine;
EXPORT_SYMBOL(x86_apple_machine);

void __init early_platform_quirks(void)
{
	x86_apple_machine = dmi_match(DMI_SYS_VENDOR, "Apple Inc.") ||
			    dmi_match(DMI_SYS_VENDOR, "Apple Computer, Inc.");
}
