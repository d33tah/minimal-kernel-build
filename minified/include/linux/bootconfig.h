/* SPDX-License-Identifier: GPL-2.0 */
/* Minimal stub - CONFIG_BOOT_CONFIG disabled */

#ifndef _LINUX_XBC_H
#define _LINUX_XBC_H

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/types.h>
#endif

#define BOOTCONFIG_MAGIC	"#BOOTCONFIG\n"
#define BOOTCONFIG_MAGIC_LEN	12

/* Minimal implementation for checksum verification */
static inline __init uint32_t xbc_calc_checksum(void *data, uint32_t size)
{
	unsigned char *p = data;
	uint32_t ret = 0;

	while (size--)
		ret += *p++;

	return ret;
}

static inline const char *xbc_get_embedded_bootconfig(size_t *size)
{
	return NULL;
}

#endif
