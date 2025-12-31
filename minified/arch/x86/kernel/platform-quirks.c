#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/bios_ebda.h>

void __init x86_early_init_platform_quirks(void)
{
	/* i8042, rtc, warm_reset, devices.pnpbios removed - never read */
	x86_platform.legacy.reserve_bios_regions = 0;

	switch (boot_params.hdr.hardware_subarch) {
	case X86_SUBARCH_PC:
		x86_platform.legacy.reserve_bios_regions = 1;
		break;
		/* XEN, INTEL_MID, CE4100 cases removed - only set unused fields */
	}
	/* set_legacy_features call removed - always NULL */
}

/* x86_pnpbios_disabled removed - never called */
