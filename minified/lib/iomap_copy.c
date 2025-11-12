// SPDX-License-Identifier: GPL-2.0-only
/*
 * MMIO copy functions - STUBBED
 */

#include <linux/export.h>
#include <linux/io.h>

void __attribute__((weak)) __iowrite32_copy(void __iomem *to,
					    const void *from,
					    size_t count)
{
}
EXPORT_SYMBOL_GPL(__iowrite32_copy);

void __ioread32_copy(void *to, const void __iomem *from, size_t count)
{
}
EXPORT_SYMBOL_GPL(__ioread32_copy);

void __attribute__((weak)) __iowrite64_copy(void __iomem *to,
					    const void *from,
					    size_t count)
{
}
EXPORT_SYMBOL_GPL(__iowrite64_copy);
