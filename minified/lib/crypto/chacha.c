// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ChaCha stub implementation
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <crypto/chacha.h>

void chacha_block_generic(u32 *state, u8 *stream, int nrounds)
{
	/* Stub - do nothing */
}
EXPORT_SYMBOL(chacha_block_generic);

void hchacha_block_generic(const u32 *state, u32 *stream, int nrounds)
{
	/* Stub - do nothing */
}
EXPORT_SYMBOL(hchacha_block_generic);
