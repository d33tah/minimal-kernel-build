// SPDX-License-Identifier: GPL-2.0
/*
 * vc_screen.c - Stubbed for minimal kernel
 * Provides /dev/vcs* devices - not needed for "Hello World"
 */

#include <linux/kernel.h>
#include <linux/init.h>

/* Public API stubs */
void vcs_make_sysfs(int index)
{
	/* Stub - /dev/vcs* not needed for minimal output */
}

void vcs_remove_sysfs(int index)
{
	/* Stub */
}

int vcs_init(void)
{
	return 0; /* Stub */
}
