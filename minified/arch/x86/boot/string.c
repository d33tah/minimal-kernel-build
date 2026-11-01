// SPDX-License-Identifier: GPL-2.0-only
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

/*
 * Very basic string functions
 */

#include <linux/types.h>
#include <asm/asm.h>

/*
 * Undef these macros so that the functions that we provide
 * here will have the correct names regardless of how string.h
 * may have chosen to #define them.
 */
#undef memcpy
#undef memset
#undef memcmp

int memcmp(const void *s1, const void *s2, size_t len)
{
	bool diff;
	asm("repe; cmpsb" CC_SET(nz)
	    : CC_OUT(nz)(diff), "+D"(s1), "+S"(s2), "+c"(len));
	return diff;
}
