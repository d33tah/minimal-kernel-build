// SPDX-License-Identifier: GPL-2.0
/*
 * SHA1 stub implementation
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <crypto/sha1.h>

void sha1_transform(__u32 *digest, const char *data, __u32 *array)
{
	/* Stub - do nothing */
}
EXPORT_SYMBOL(sha1_transform);

void sha1_init(__u32 *buf)
{
	/* Stub - do nothing */
}
EXPORT_SYMBOL(sha1_init);
