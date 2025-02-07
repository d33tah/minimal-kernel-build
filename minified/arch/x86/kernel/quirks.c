// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains work-arounds for x86 and x86_64 platform bugs.
 */
#include <linux/dmi.h>
#include <asm/setup.h>

#include "linux/export.h"
#include "linux/init.h"
#include "linux/mod_devicetable.h"
#include "linux/types.h"





bool x86_apple_machine;
EXPORT_SYMBOL(x86_apple_machine);

void __init early_platform_quirks(void)
{
	x86_apple_machine = dmi_match(DMI_SYS_VENDOR, "Apple Inc.") ||
			    dmi_match(DMI_SYS_VENDOR, "Apple Computer, Inc.");
}
