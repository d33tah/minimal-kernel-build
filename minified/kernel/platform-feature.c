// SPDX-License-Identifier: GPL-2.0

#include <linux/bitops.h>
#include <linux/cache.h>
#include <linux/export.h>
#include <linux/platform-feature.h>

/* Platform feature tracking - STUBBED */

void platform_set(unsigned int feature)
{
}
EXPORT_SYMBOL_GPL(platform_set);

void platform_clear(unsigned int feature)
{
}
EXPORT_SYMBOL_GPL(platform_clear);

bool platform_has(unsigned int feature)
{
	return false;
}
EXPORT_SYMBOL_GPL(platform_has);
