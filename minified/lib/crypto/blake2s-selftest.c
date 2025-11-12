// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include <crypto/internal/blake2s.h>
#include <linux/string.h>

/* Stubbed for minification - self-tests not needed for minimal boot */

bool __init blake2s_selftest(void)
{
	return true;
}
