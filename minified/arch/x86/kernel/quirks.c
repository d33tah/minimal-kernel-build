/* Early platform quirks - minimal includes */
#include <linux/dmi.h>





bool x86_apple_machine;

void __init early_platform_quirks(void)
{
	x86_apple_machine = dmi_match(DMI_SYS_VENDOR, "Apple Inc.") ||
			    dmi_match(DMI_SYS_VENDOR, "Apple Computer, Inc.");
}
