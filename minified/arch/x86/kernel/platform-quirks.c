#include <linux/init.h>

#include <asm/setup.h>
#include <asm/bios_ebda.h>

void __init x86_early_init_platform_quirks(void)
{
	x86_platform.legacy.reserve_bios_regions = 0;

	switch (boot_params.hdr.hardware_subarch) {
	case X86_SUBARCH_PC:
		x86_platform.legacy.reserve_bios_regions = 1;
		break;
	}
}

